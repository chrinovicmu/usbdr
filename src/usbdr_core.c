#include <linux/kernel.h> 
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/byteorder/generic.h> 
#include "usbdr.h"

#define USBDR_VENDOR_ID     0x1234
#define USBDR_PRODUCT_ID    0x5678 


/*max minor device numbers*/ 
#define USBDR_MAX_DEVICES   16

extern const struct file_operations usbdr_fops; 

static dev_t usbdr_devt; 

static struct class *usbdr_class; 

struct usb_driver usbdr_driver; 

static int submit_intr_in_urb(struct usbdr_dev *dev)
{
    int ret; 

    if(!dev->intr_in_urb || !dev->intr_in_buffer)
        return -EINVAL; 

    usb_fill_int_urb(
        dev->intr_in_urb,
        dev->udev, 
        dev->intr_in_pipe,
        dev->intr_in_size,
        dev->intr_in_callback, 
        dev,
        dev->intr_in_interval
    ); 

    ret = usb_submit_urb(dev->intr_in_urb, GFP_KERNEL); 
    if(ret)
        dev_err(&dev->interface->dev, "Failed to submit interrupt IN URB: %d\n", ret); 

    return ret; 
}

static int usbdr_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct usbdr_dev *dev; 

    /*acttive altsetting */ 
    struct usb_host_interface *iface_desc = intf->cur_altsetting; 

    struct usb_endpoint_descriptor *ep;
    int i, retval = -ENOMEM; 

    dev = kzalloc(sizeof(*dev), GFP_KERNEL); 
    if(!dev)
        return -ENOMEM;

    mutex_init(&dev->io_mutex); 
    mutex_init(&dev->urb_mutex); 

    init_waitqueue_head(&dev->bulk_in_wait); 
    init_waitqueue_head(&dev->bulk_out_wait);
    init_waitqueue_head(&dev->intr_in_wait); 

    atomic_set(&dev->open_count, 0); 

    /*store usb_device pointer and increment it's refcount */ 
    dev->udev = usb_get_dev(interface_to_usbdev(intf)); 
    dev->interface = intf; 


    /*cache vendor/product IDs*/
    dev->vendor_id = le16_to_cpu(dev->udev->descriptor.idVendor); 
    dev->product_id = le16_to_cpu(dev->udev->descriptor.idProduct); 
    dev->device_class = dev->udev->descriptor.bDeviceClass;
    dev->device_subclass = dev->udev->descriptor.bDeviceProtocol; 

    /*scan all endpoints in the interface descriptor */ 

    for(i = 0; i < iface_desc->desc.bNumEndpoints; i++)
    {
        /*ep points to endpoint descriptor of each endpoint */ 
        ep = iface_desc->endpoint[i].desc;

        if(!dev->bulkin && usb_endpoint_is_bulk_in(ep))
        {
            dev->bulkin = ep; /*store endpoint desc*/

            /*create pipe */
            dev->bulk_in_pipe = usb_rcvbulkpipe(dev->udev, ep->bEndpointAddress); 

            /*allocate buffer */ 
            dev->bulk_in_size = usb_endpoint_maxp(ep) * 8;

            /*allocate DMA-coherent buffer */ 
            dev->bulk_in_buffer = usb_alloc_coherent(
                dev->udev, 
                dev->bulk_in_size, 
                GFP_KERNEL, 
                &dev->bulk_in_dma); 


            dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL); 
        }

        if(!dev->bulkout && usb_endpoint_is_bulk_out(ep))
        {
            dev->bulkout = ep; 
            dev->bulk_out_pipe = usb_sndbulkpipe(dev->udev, ep->bEndpointAddress); 
            dev->bulk_out_size = usb_endpoint_maxp(ep) * 8; 

            dev->bulk_out_buffer = usb_alloc_coherent(
                dev->udev, 
                dev->bulk_out_size, 
                GFP_KERNEL,
                &dev->bulk_out_dma); 

            dev->bulk_out_urb = usb_alloc_urb(0, GFP_KERNEL); 
        }

        if(!dev->intr_in && usb_endpoint_is_int_in(ep))
        {
            dev->intr_in = ep; 
            dev->intr_in_pipe = usb_rcvintpipe(dev->udev, ep->bEndpointAddress); 
            dev->intr_in_size = usb_endpoint_maxp(ep); 

            dev->intr_in_buffer = usb_alloc_coherent(
                dev->udev, 
                dev->intr_in_size,
                GFP_KERNEL, 
                &dev->intr_in_dma);  

            dev->intr_in_urb = usb_alloc_urb(0, GFP_KERNEL); 
        }
    }

    if(!dev->bulkin || !dev->bulkout)
    {
        retval = -ENODEV;
        goto err_free_all; 
    }

    /*character device registration */ 
    cdev_init(&dev->cdev, &usbdr_fops); 
    dev->cdev.owner = THIS_MODULE; 

    cdev_add(&dev->cdev, MKDEV(MAJOR(usbdr_devt), intf->minor), 1); 

    /*node in sysfs and /dev */ 
    dev->dev = device_create(usbdr_class,
                             &intf->dev,
                             MKDEV(MAJOR(usbdr_devt), intf->minor), 
                             NULL, 
                             "usbdr%d", intf->minor
                             ); 

    usb_set_intfdata(intf, dev);

    if(dev->intr_in_urb)
        submit_intr_in_urb(dev); 

    return 0; 

err_free_all:
    usb_put_dev(dev->udev);              
    kfree(dev->bulk_in_buffer);         
kfree(dev->bulk_out_buffer);
    kfree(dev->intr_in_buffer);
    usb_free_urb(dev->bulk_in_urb);     
    usb_free_urb(dev->bulk_out_urb);
    usb_free_urb(dev->intr_in_urb);
    kfree(dev); 

    return retval; 
}


static void usbdr_disconnect(struct usb_interface *intf)
{
    struct usbdr_dev *dev = usb_get_intfdata(intf); 
    if(!dev)
        return; 

    dev->disconnected = true; 

    /*kill outstanding URBs */ 

    mutex_lock(&dev->io_mutex); 
    
    if(dev->bulk_in_urb)
        usb_kill_urb(dev->bulk_in_urb); 
    if(dev->bulk_out_urb)
        usb_kill_urb(dev->bulk_out_urb);
    if(dev->intr_in_urb)
        usb_kill_urb(dev->intr_in_urb); 

    mutex_unlock(&dev->io_mutex); 

    device_destroy(usbdr_class, dev->cdev.dev);
    cdev_del(&dev->cdev); 

    usb_set_intfdata(intf, NULL); 
    usb_put_dev(dev->udev); 

    kfree(dev->bulk_in_buffer);
    kfree(dev->bulk_out_buffer);
    kfree(dev->intr_in_buffer);
    usb_free_urb(dev->bulk_in_urb);
    usb_free_urb(dev->bulk_out_urb);
    usb_free_urb(dev->intr_in_urb);
    kfree(dev); 

    dev_info(&intf->dev, "USB device disconnected\n"); 

}

static const struct usb_device_id usbdr_table[] = {
    {USB_DEVICE(USBDR_VENDOR_ID, USBDR_PRODUCT_ID) }, 
    {}
}; 

MODULE_DEVICE_TABLE(usb, usbdr_table); 


struct usb_driver usbdr_driver = {
    .name = "usbdr", 
    .id_table = usbdr_table, 
    .probe = usbdr_probe, 
    .disconnect = usbdr_disconnect, 
    .supports_autosuspend = 1, 
};

static int __init usbdr_init(void)
{
    int ret; 

    ret = alloc_chrdev_region(
        &usbdr_devt,
        0,
        USBDR_MAX_DEVICES, 
        "usbdr"); 

    if(ret)
        return ret; 

    usbdr_class = class_create(THIS_MODULE, "usbdr"); 
    if(IS_ERR(usbdr_class))
    {
        ret = PTR_ERR(usbdr_class); 
        goto err_unreg; 
    }

    ret = usb_register(&usbdr_driver); 
    if(ret)
        goto err_class;

    pr_info("usbdr: driver registered (major %d)\n", MAJOR(usbdr_devt)); 

    return 0; 

err_class:
    class_destroy(usbdr_class); 
err_unreg:
    unregister_chrdev_region(usbdr_devt, USBDR_MAX_DEVICES); 
    return ret; 
}

static void __exit usbdr_exit(void)
{
    usb_deregister(&usbdr_driver); 
    class_destroy(usbdr_class); 
    unregister_chrdev_region(usbdr_devt, USBDR_MAX_DEVICES); 
    pr_info("usbdr: driver unloaded\n"); 
}

module_init(usbdr_init); 
module_exit(usbdr_exit); 

MODULE_AUTHOR("Chrinovic M");
MODULE_DESCRIPTION("USB Driver");
MODULE_LICENSE("GPL");  

