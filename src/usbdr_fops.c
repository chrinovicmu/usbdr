#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include "usbdr.h"

extern struct usb_driver usbdr_driver; 


/* Callback for Bulk IN transfer completion */
void usbdr_bulk_in_callback(struct urb *urb)
{
    struct usbdr_dev *dev = urb->context;

    mutex_lock(&dev->io_mutex);

    switch (urb->status) 
    {
        case 0: /* Success */
            pr_info("Bulk IN URB completed successfully\n");
            wake_up_interruptible(&dev->bulk_in_wait);
            break;

        case -EPIPE: /* Endpoint stalled */
            pr_err("Bulk IN URB stalled (endpoint halted)\n");
            usb_clear_halt(dev->udev, usb_pipeendpoint(urb->pipe));
            wake_up_interruptible(&dev->bulk_in_wait);
            break;

        case -ENOENT:       
        case -ECONNRESET:   
        case -ESHUTDOWN:
            pr_err("Bulk IN URB canceled or device disconnected (status %d)\n", urb->status);
            dev->disconnected = true;
            wake_up_interruptible(&dev->bulk_in_wait);
            break;

        default:
            pr_err("Bulk IN URB error, status: %d\n", urb->status);
            wake_up_interruptible(&dev->bulk_in_wait);
            break;
    }

    mutex_unlock(&dev->io_mutex);
    complete(&dev->bulk_in_done);
}

/* Callback for Bulk OUT transfer completion */
void usbdr_bulk_out_callback(struct urb *urb)
{
    struct usbdr_dev *dev = urb->context;

    mutex_lock(&dev->io_mutex);

    if (urb->status != 0)
        pr_err("Bulk OUT URB failed with status: %d\n", urb->status);

    wake_up_interruptible(&dev->bulk_out_wait);

    mutex_unlock(&dev->io_mutex);

    complete(&dev->bulk_out_done);
}

/* Callback for Interrupt IN transfer completion */
void usbdr_intr_in_callback(struct urb *urb)
{
    struct usbdr_dev *dev = urb->context;

    mutex_lock(&dev->io_mutex);

    if (urb->status == 0) {
        pr_info("Interrupt IN URB completed\n");
        wake_up_interruptible(&dev->intr_in_wait);
    } else {
        pr_err("Interrupt IN URB error, status: %d\n", urb->status);
    }

    mutex_unlock(&dev->io_mutex);
}

static int usbdr_open(struct inode *inode, struct file *file)
{
    struct usbdr_dev *dev; 

    /*locate usb interface for minor number encoded in the inode */ 
    struct usb_interface *inf = usb_find_interface(&usbdr_driver, iminor(inode)); 
    if(!inf)
        return -ENODEV;

    if(!atomic_add_unless(&dev->open_count, 1, 2))
        return -EBUSY;

    mutex_lock(&dev->io_mutex); 
    file->private_data = dev; 
    mutex_unlock(&dev->io_mutex);

    return 0; 
}

static int usbdr_release(struct inode *inode, struct file *file)
{
    struct usbdr_dev *dev = file->private_data; 

    if(dev)
        atomic_dec(&dev->open_count); 
    
    return 0; 
}

static ssize_t usbdr_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
    struct usbdr_dev *dev = file->private_data; 
    int retval, actual_len; 

    /*ensure bulk-in enpoint is configured to read from */ 
    if(!dev->bulk_in_pipe)
        return -ENODEV;

    mutex_lock(&dev->io_mutex); 

    if(dev->disconnected)
    {
        retval = -ENODEV;
        goto _exit; 
    }
    
    reinit_completion(&dev->bulk_in_done); 

    /*prepare urb */ 
    usb_fill_bulk_urb(dev->bulk_in_urb,
                      dev->udev,
                      dev->bulk_in_pipe, 
                      dev->bulk_in_buffer, 
                      min(count, dev->bulk_in_size), 
                      usbdr_bulk_in_callback,
                      dev);

    /*mark the urb as not requiring DMA mapping for this submisson. 
     * assume buffer is already DMA-able*/ 

    dev->bulk_in_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP; 

    retval = usb_submit_urb(dev->bulk_in_urb, GFP_KERNEL); 
    if(retval)
    {
        pr_info("usb_fops: urb submisson failed!\n"); 
        goto _exit; 
    }

    retval = wait_for_completion_interruptible(&dev->bulk_in_done); 
    if(retval)
    {
        usb_kill_urb(dev->bulk_in_urb); 
        pr_err("usbdr_read: wait_for_completion_interruptible interrupted, killing URB\n"); 
        usb_kill_urb(dev->bulk_in_urb); 
        goto _exit; 
    }

    if(dev->bulk_in_urb->actual_length == 0)
    {
        pr_err("usbdr_read: Bulk IN URB completed but no data was received\n");
        retval = -EIO; 
        goto _exit; 
    }

    pr_info("usbdr_read: Bulk IN completed succesffuly, %d bytes received\n", 
            dev->bulk_in_urb->actual_length);

    actual_len = dev->bulk_in_urb->actual_length; 

    /*copy from kernel buffer to userspace memory  */
    if(actual_len && copy_to_user(buf, dev->bulk_in_buffer, actual_len))
    {
        pr_err("usbdr_read: failed to copy data from kernel buffer to userspace"); 
        retval = -EFAULT; 
    }else
        retval = actual_len; 

_exit:
    return retval; 

}

static ssize_t usbdr_write(struct file *file, const char __user *buf, size_t count, loff_t *pos) 
{
    struct usbdr_dev *dev = file->private_data; 
    unsigned char *kbuf = dev->bulk_out_buffer;
    int retval;

    /*check if bulk OUT endpoint has been initialzed */ 
    if(!dev->bulk_out_pipe)
        return -ENODEV; 

    /*handle 0 length writes */ 
    if(count == 0)
        return 0;

    if(count > dev->bulk_out_size)
        count = dev->bulk_out_size; 

    /*copy data from userspace to kernel buffer*/ 
    if(copy_from_user(kbuf, buf, count))
        return -EFAULT; 

    mutex_lock(&dev->io_mutex); 

    if(dev->disconnected)
    {
        retval = -ENODEV; 
        goto _exit; 
    }

    reinit_completion(&dev->bulk_out_done); 

    usb_fill_bulk_urb(dev->bulk_out_urb, 
                 dev->udev, 
                 dev->bulk_out_pipe, 
                 kbuf, 
                 count, 
                 usbdr_bulk_out_callback, 
                 dev); 

    dev->bulk_out_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP; 

    retval = usb_submit_urb(dev->bulk_out_urb, GFP_KERNEL); 
    if(retval)
    {
        pr_info("usbdr_write: usb_submit_urb failed: %d\n", retval); 
        goto _exit; 
    }

    retval = wait_for_completion_interruptible(&dev->bulk_out_done);
    if(retval)
    {
        pr_err("usbdr_write: wait_for_completion_interruptible interrupted, killing URB\n"); 
        usb_kill_urb(dev->bulk_out_urb); 
        goto _exit; 
    }

    retval = dev->bulk_out_urb->actual_length ? dev->bulk_out_urb->actual_length : count; 

_exit:
    mutex_unlock(&dev->io_mutex); 
    return retval; 

}

const struct file_operations usbdr_fops = {
    .owner = THIS_MODULE,
    .open = usbdr_open, 
    .release = usbdr_release, 
    .read = usbdr_read, 
    .write = usbdr_write, 
    .llseek = no_llseek,
}; 


