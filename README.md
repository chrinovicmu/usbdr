# USBDR Linux Kernel Driver

**USBDR** is a Linux kernel module that provides support for a custom USB device with vendor ID `0x1234` and product ID `0x5678`. It demonstrates basic USB driver functionality including interrupt and bulk transfers.

---

## Features

- USB device probe and disconnect handling
- Bulk and interrupt endpoint support
- User-space interaction through character device interface

---

## Requirements

- Linux kernel headers installed (tested on 6.1.x)
- `make` and `gcc` installed
- Root privileges to load kernel modules

---

## Building

Clone the repository and build the module:

```bash
git clone https://github.com/chrinovicmu/usbdr
cd usbdr
make
```

---

## Installation

Load the kernel module:

```bash
sudo insmod usbdr.ko
```

Verify the module is loaded:

```bash
lsmod | grep usbdr
dmesg | tail
```

---

## Usage

Once loaded, the driver will automatically detect and attach to USB devices with vendor ID `0x1234` and product ID `0x5678`. The device will be accessible through `/dev/usbdr0` (or `/dev/usbdr1`, etc. for multiple devices).

---

## Unloading

Remove the kernel module:

```bash
sudo rmmod usbdr
```

---

## License

[GPL]
