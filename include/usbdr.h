#ifndef USBDR_H
#define USBDR_H

#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/cdev.h>

struct usbdr_dev
{
    struct usb_device *udev;
    struct usb_interface interface; 

    unsigned char minor; 

    /*device descriptors */ 
    __u16 vendor_id;
    __u8 product_id; 
    __u8 device class; 
    __u8 device_subclass; 
    __u8 device_protocol; 

    struct usb_endpoint_descriptor *bulkin; /*bulk IN endpoint */ 
    struct usb_endpoint_descriptor *bulkout; /* bulk OUT endpoint */ 
    struct usb_endpoint_descriptor *intr_in; /*Interrupt IN endpoint */ 

  
    /*endpoint addresses (cached for quik access in read/write) */ 
    unsigned int bulk_in_pipe;
    unsigned int bulk_out_pipe; 
    unsigned intr_in_pipe; 

    /*URB and complettion handling */ 
    struct urb *bulk_in_urb; 
    struct urb *bulk_out_urb; 
    struct urb *intr_in_urb; 

    struct completion bulk_in_done; 
    struct completion bulk_out_done; 

    /*buffer*/ 
    unsigned char *bulk_in_buffer;  /*allocated buffer for bulk IN data */ 
    unsigned char *bulk_out_buffer; 
    unsigned char *intr_in_buffer; /*allocated buffer for bulk In endpoit */ 

    /*buffer DMA addresses */ 
    dma_addr_t bulk_in_dma; 
    dma_addr_t bulk_out_dma; 
    dma_addr_t intr_in_dma; 

    size_t bulk_in_size; 
    size_t bulk_out_size; 
    size_t intr_in_size; 

    /*device state and synchronization */ 
    atomic_t open_count; /*num of open file handles */ 
    struct mutex io_mutex; 
    struct mutex urb_mutex; 

    /*wait queue for blocking reans on bulk IN */ 
    wait_queue_head_t bulk_in_wait; 
    wait_queue_head_t bulk_out_wait; 
    wait_queue_head_t intr_in_wait; 

    bool ongoing_read;   
    bool disconnected; 
 
    /*chatacter device integration */ 
    struct device *dev; 
    struct cdev cdev;

    void *private_data;
}; 

