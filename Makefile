
# Kernel module Makefile

obj-m := usbdr.o
usbdr-objs := src/usbdr_core.o src/usbdr_fops.o

# Path to kernel build directory
KDIR := /lib/modules/$(shell uname -r)/build

# Default target: build the module
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Clean up
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
