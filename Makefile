#
# Makefile for the linux dsmfs routines.
#

MY_CFLAGS += -g -DDEBUG
ccflags-y += ${MY_CFLAGS}
CC += ${MY_CFLAGS}

KDIR ?= /lib/modules/`uname -r`/build

default:
	./generate_exports.sh
	$(MAKE) -C $(KDIR) M=$$PWD
clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean
