#sudo modprobe netconsole netconsole=6665@192.168.1.10/eth0,6666@192.168.1.20/52:54:00:12:34:56

sudo modprobe netconsole netconsole=6665@192.168.71.128/ens33,6666@192.168.71.129/00:0c:29:04:3f:d0
sudo dmesg -n 8
echo "<1>NETCONSOLE TEST MESSAGE" | sudo tee /dev/kmsg

