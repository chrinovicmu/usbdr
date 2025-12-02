# Change directory to the kernel build directory
KERNEL_DIR := /lib/modules/$(shell uname -r)/build

# Module name
MODULE_NAME := usbdr

# Source files
SRC_FILES := src/usbdr_core.c src/usbdr_fops.c

# Header directory
INC_DIR := include

# Set the include path
ccflags-y := -I$(PWD)/$(INC_DIR)

# Create the module object
obj-m := $(MODULE_NAME).o
$(MODULE_NAME)-y := $(SRC_FILES:.c=.o)

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f *.o *.ko *.mod.c *.mod.o modules.order Module.symvers
	rm -f src/*.o

install:
	sudo insmod $(MODULE_NAME).ko

uninstall:
	sudo rmmod $(MODULE_NAME)

reload: uninstall install
