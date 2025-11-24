#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include "usbr.h"

extern struct usb_driver usbdr_driver; 

static int usbdr_open(struct inode *inode, struct file *file)
{
    struct usbdr_dev *dev; 

    /*locate usb interface for minor number encoded in the inode */ 
    struct usb_interface *inf = usb_find_interface(&usbdr_driver, iminor(inode)); 
    if(!inf)
        return -ENODEV;

    if(atomic_add_unle)
}
