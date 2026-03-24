# The name of the resulting kernel module (.ko file)
MODULE_NAME := syscall-throttle

# List the object files needed. 
# We prefix them with src/ so Kbuild finds them in the subfolder.
$(MODULE_NAME)-objs := src/main.o src/preemptable_kprobe.o src/throttle.o src/timer.o src/driver.o src/monitor.o

# Tell the kernel build system about our module
obj-m := $(MODULE_NAME).o

# Path to the running kernel's build directory
KDIR := /lib/modules/$(shell uname -r)/build
# Current working directory
PWD := $(shell pwd)

# Add our include directory to the header search path
# -I$(PWD)/include tells the compiler where to find your .h files
# EXTRA_CFLAGS := -I$(PWD)/include
ccflags-y := -I$(src)/include

# syscall-throttle-objs := main.o preemptable_kprobe.o throttle.o timer.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules 

clean:
	-mv -f compile_commands.json /tmp
	$(MAKE) -C $(KDIR) M=$(PWD) clean 
	-mv -f /tmp/compile_commands.json .

bear:
	-rm compile_commands.json
	-$(MAKE) clean
	bear -- $(MAKE)

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
# 	sudo dmesg

retry:
	$(MAKE) untry
	$(MAKE) try

