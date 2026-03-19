obj-m += syscall-throttle.o 

syscall-throttle-objs := main.o preemptable_kprobe.o throttle.o timer.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	-mv -f compile_commands.json /tmp
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	-mv -f /tmp/compile_commands.json .

bear:
	-rm compile_commands.json
	-$(MAKE) clean
	bear -- make

insmod:
	sudo insmod syscall-throttle.ko

rmmod:
	sudo rmmod syscall-throttle

untry:
	-sudo rmmod syscall-throttle
	$(MAKE) clean

try:
	$(MAKE) all
	sudo insmod syscall-throttle.ko
	sudo dmesg

retry:
	$(MAKE) untry
	$(MAKE) try

