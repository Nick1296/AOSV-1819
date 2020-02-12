# Module name
obj-m += SessionFS.o
# objects that from the module
SessionFS-objs+=device_sessionfs.o module.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
	gcc -o userspace_test userspace_test.c

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
	rm userspace_test userspace_test.o

	#quick test to see if the module is loaded correctly
insmod:
	sudo dmesg -C
	sudo insmod SessionFS.ko
	./userspace_test
	sudo rmmod SessionFS.ko
	sudo dmesg
