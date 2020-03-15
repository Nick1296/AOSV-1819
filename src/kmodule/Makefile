# Module name
obj-m += SessionFS.o
# objects that from the module
SessionFS-objs+=session_info.o session_manager.o device_sessionfs.o module.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

#quick test to see if the module is loaded correctly
insmod: all
	sudo dmesg -C
	sudo dmesg -w -H &
	sudo insmod SessionFS.ko
rmmod:
	sudo rmmod SessionFS
	killall dmesg
