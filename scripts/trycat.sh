sudo dmesg -C
make retry
cat /proc/SYSCALL-THROTTLE/program_names
for i in {0..1023}
    do echo "Inserting $i..."
    sudo ../user/ioctl.out 6 xyz$i
done

