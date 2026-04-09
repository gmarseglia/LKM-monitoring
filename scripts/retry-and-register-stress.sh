#!/bin/bash
sudo dmesg -C
make -C ../module retry
make -C ../user all
sudo /home/ghi/LKM-monitoring/user/ioctl.out 0
sudo /home/ghi/LKM-monitoring/user/ioctl.out 2 0
for i in {0..511}; do
	echo xyz$i
	sudo /home/ghi/LKM-monitoring/user/ioctl.out 4 xyz$i
	sudo /home/ghi/LKM-monitoring/user/ioctl.out 6 xyz$i
done
