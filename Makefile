modname := bbswitch
obj-m := $(modname).o

KVERSION := $(shell uname -r)
KDIR := /lib/modules/$(KVERSION)/build
PWD := "$$(pwd)"

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

load:
	-/sbin/rmmod $(modname)
	/sbin/insmod $(modname).ko

install_dkms_module:
	./scripts/install_dkms.sh install

uninstall_dkms_module:
	./scripts/install_dkms.sh uninstall
