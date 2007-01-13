ifneq ($(KERNELRELEASE),)
obj-m := kfusd.o
else
KDIR	?= /lib/modules/$(shell uname -r)/build
PWD	:= $(shell pwd)

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) EXTRA_CFLAGS=-I$(PWD)/../include modules

install: 
	$(INSTALL) -d -m 0755 /lib/modules/$(shell uname -r)/kernel/drivers/misc
	$(INSTALL) -m 0755 kfusd.ko /lib/modules/$(shell uname -r)/kernel/drivers/misc
	/sbin/depmod -a

clean:
	rm -f .kfusd* Modules.symvers \
		kfusd.ko kfusd.o kfusd.mod.o kfusd.mod.c built-in.o *~
	rm -rf .tmp_versions
endif
