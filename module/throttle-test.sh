#!/bin/bash
sudo dmesg -C
make retry
sudo /home/ghi/LKM-monitoring/user/ioctl.out 6 user.out
#sudo /home/ghi/LKM-monitoring/user/ioctl.out 2 1
sudo /home/ghi/LKM-monitoring/user/ioctl.out 2 0
sudo /home/ghi/LKM-monitoring/user/ioctl.out 0

