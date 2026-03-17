
obj-m += syscall-throttle.o 

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	@mv compile_commands.json /tmp
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	@mv /tmp/compile_commands.json .

insmod:
	sudo insmod syscall-throttle.ko

rmmod:
	sudo rmmod syscall-throttle
