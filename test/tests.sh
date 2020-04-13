#! /bin/sh
dmesg -C
echo "starting test"
insmod ../src/kmodule/SessionFS.ko
make lib-test-single > test1.log
rmmod SessionFS
dmesg -c > dmesg1.log
echo "test 1 done"
insmod ../src/kmodule/SessionFS.ko
make lib-stress-test-single > test2.log
rmmod SessionFS
dmesg -c > dmesg2.log
echo "test 2 done"
for ((i = 0 ; i < 5 ; i++)); do
	insmod ../src/kmodule/SessionFS.ko
	make lib-test-multi > test3-${i}.log
	rmmod SessionFS
	dmesg -c > dmesg3-${i}.log
	echo "test 3-${i} done"
done
insmod ../src/kmodule/SessionFS.ko
make lib-stress-test-multi > test4.log
rmmod SessionFS
dmesg -c > dmesg4.log
echo "test 4 done"
make clean
echo "test directory cleaned"
