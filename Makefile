#
# Makefile for the linux dsmfs routines.
#


obj-m += dsmfs.o 
dsmfs-objs += init.o file-mmu.o inode.o channel.o dsm.o unmap.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
