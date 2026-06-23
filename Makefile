# SPDX-License-Identifier: GPL-2.0
#
# Out-of-tree build. kbuild reads the module spec (obj-m, ccflags) from Kbuild;
# this drives the outer build. KERNEL_SRC is supplied by Yocto's module.bbclass
# (= STAGING_KERNEL_DIR); it defaults to the running kernel for a standalone
# build. For a cross build pass ARCH/CROSS_COMPILE:
#
#   make KERNEL_SRC=/path/to/linux ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
SRC := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) clean

.PHONY: all modules_install clean
