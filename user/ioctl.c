#include <errno.h>
#include <stdlib.h>
#include <string.h>
#define _XOPEN_SOURCE
#include "syscall-ioctl.h"
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h> // Header for pid_t
#include <time.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/sys_thr"

void msleep(long ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000;
  nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {

  pid_t process_id = getpid();
  pid_t parent_id = getppid();

  printf("Current Process ID (PID): %d\n", process_id);
  printf("Parent Process ID (PPID): %d\n", parent_id);

  int fd_char = open(DEVICE_PATH, O_RDONLY);

  if (fd_char < 0) {
    perror("Failed to open DEVICE_PATH");
    return 1;
  }

  unsigned int command;
  unsigned int argv1;
  sscanf(argv[1], "%d", &argv1);
  switch (argv1) {
  case 1:
    command = IOCTL_REGISTER_PID;
    break;
  case 2:
    command = IOCTL_UNREGISTER_PID;
    break;
  default:
    printf("got command=%d\n", command);
    return -1;
  }

  // unsigned long param;
  // sscanf(argv[2], "%lu", &param);
  // printf("command=%d, param=%lu", command, param);

  if (ioctl(fd_char, command, argv[2]) < 0) {
    printf("ioctl failed with error %d: %s\n", errno, strerror(errno));
  }

  close(fd_char);

  return 0;
}
