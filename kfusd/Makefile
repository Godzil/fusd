ifneq ($(KERNELRELEASE),)
obj-m := kfusd.o
KERNEL_VER ?= $(KERNELRELEASE)
else
KDIR	?= /lib/modules/$(shell uname -r)/build
PWD	:= $(shell pwd)
ROOTFS ?=
GIT_DESCRIBE = $(shell git describe --dirty --tags)
KERNEL_VER ?= 2.6.32.7

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) EXTRA_CFLAGS="-I$(PWD)/../include -DGIT_DESCRIBE='\"${GIT_DESCRIBE}\"'" V=1 modules

install:
	install -d -m 0755 $(ROOTFS)/lib/modules/$(KERNEL_VER)/kernel/drivers/misc
	install -m 0755 kfusd.ko $(ROOTFS)/lib/modules/$(KERNEL_VER)/kernel/drivers/misc

#	/sbin/depmod -a

clean:
	rm -f .kfusd* Modules.symvers \
		kfusd.ko kfusd.o kfusd.mod.o kfusd.mod.c built-in.o *~
	rm -rf .tmp_versions
endif
