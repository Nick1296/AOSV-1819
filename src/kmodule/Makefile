# Module name
obj-m += SessionFS.o
# objects that from the module
SessionFS-objs+=session_info.o session_manager.o device_sessionfs.o module.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

#quick test to see if the module is loaded correctly
test:
	sudo dmesg -C
	sudo insmod SessionFS.ko
	sudo rmmod SessionFS.ko
	sudo dmesg
