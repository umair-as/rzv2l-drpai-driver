# SPDX-License-Identifier: GPL-2.0
#
# Out-of-tree build of the RZ/V2L DRP-AI kernel driver as a SINGLE module.
#
# In the original Renesas tree the two objects are compiled into vmlinux
# (drivers/drpai/Makefile: obj-$(CONFIG_DRPAI) += drpai-core.o drpai-if.o,
# expecting CONFIG_DRPAI=y). drpai-core.c has no module_init and is not
# EXPORT_SYMBOL-linked to drpai-if.c, so the two cannot load as independent
# .ko's. Linking both objects into one drpai.ko resolves drpai-if's calls
# into drpai-core within the module.
#
# drpai.h is not present in a mainline/CIP kernel; -I$(src)/include supplies
# the bundled <linux/drpai.h> and <uapi/linux/drpai.h> (kernel headers take
# precedence for everything else, so only drpai.h falls through to here).

obj-m += drpai.o
drpai-objs := drpai-core.o drpai-if.o
ccflags-y += -I$(src)/include
# v2.30 selects the RZ/V2L code paths with this define (Renesas's module
# Makefile sets it) instead of the kernel's CONFIG_ARCH_R9A07G054.
ccflags-y += -DDRPAI_CONFIG_ARCH_RZV2L
