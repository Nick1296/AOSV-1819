# Module name
obj-m += SessionFS.o
# objects that from the module
SessionFS-objs+=module.o hooks.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

#quick test to see if the module is loaded correctly
test:
	sudo dmesg -C
	sudo insmod SessionFS.ko
	sudo rmmod SessionFS.ko
	dmesg
