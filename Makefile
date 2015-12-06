ifneq (${KERNELREALEASE},)
obj-m += sfs.o
sfs-objs = super.o inode.o adspace.o file.o dir.o bitmap.o namei.o symlink.o itree.o
else
obj-m += sfs.o
sfs-objs = super.o inode.o adspace.o file.o dir.o bitmap.o namei.o symlink.o itree.o
KERNEL_SOURCE := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(CC) mkfs.sfs.c -I. -o mkfs.sfs
	$(MAKE) -C ${KERNEL_SOURCE} SUBDIRS=$(PWD)

clean:
	rm -f *.o *.ko
endif
