# Module name
obj-m += SessionFS.o
# objects that from the module
SessionFS-objs+=module.o 

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	gcc -o userspace_test userspace_test.c

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm userspace_test userspace_test.o
#quick test to see if the module is loaded correctly
test:
	sudo dmesg -C
	sudo insmod SessionFS.ko
	./userspace_test
	sudo rmmod SessionFS.ko
	sudo dmesg 
