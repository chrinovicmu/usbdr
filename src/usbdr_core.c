#include <linux/kernel.h> 
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/device.h>
#include <sys/types.h>
#include "usbdr.h"

#define USBDR_VENDOR_ID     0x1234
#define USBDR_PRODUCT_ID    0x5678 


/*max minor device numbers*/ 
#define USBDR_MAC_DEVICES   16

extern const struct file_operations usbdr_fops; 

static dev_t usbdr_devt; 

static struct class *usbdr_class; 

struct usb_driver usbdr_driver; 


static int usbdr_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct usbdr_dev *dev; 

    /*acttive altsetting */ 
    struct usb_host_interface *iface_desc = intf->cur_altsetting; 

    struct usb_endpoint_descriptor *ep;
    int i, retval = -ENOMEM; 

    dev = kzalloc(sizeof(*dev), GFP_KERNEL); 
    if(!dev)
        retuurn -ENOMEM;

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

    /*scan all endpoints in the interface descriptor */ 

    for(i = 0; i < iface_desc->desc.bNumEndPoints; i++)
    {
        ep = iface_desc->endpoint[i].desc
    }

}


