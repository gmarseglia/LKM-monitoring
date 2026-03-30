#include "syscall-ioctl.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
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

  int fd_char = open(DEVICE_PATH, O_RDONLY);

  if (fd_char < 0) {
    perror("Failed to open DEVICE_PATH");
    return 1;
  }

  unsigned long COMMANDS[] = {
      IOCTL_START_THROTTLE,     IOCTL_STOP_THROTTLE,
      IOCTL_REGISTER_NR,        IOCTL_UNREGISTER_NR,
      IOCTL_REGISTER_EUID,      IOCTL_UNREGISTER_EUID,
      IOCTL_REGISTER_PROG_NAME, IOCTL_UNREGISTER_PROG_NAME};

  unsigned long command = -1;
  int argv1;
  int argv2;

  sscanf(argv[1], "%d", &argv1);
  if (argv1 >= 0 && argv1 < (sizeof(COMMANDS) / sizeof(unsigned long))) {
    command = COMMANDS[argv1];
  }

  switch (command) {
  case IOCTL_START_THROTTLE:
  case IOCTL_STOP_THROTTLE:
    if (ioctl(fd_char, command) < 0) {
      printf("ioctl failed with error %d: %s\n", errno, strerror(errno));
    }
    break;
  case IOCTL_REGISTER_NR:
  case IOCTL_UNREGISTER_NR:
    sscanf(argv[2], "%d", &argv2);
    if (ioctl(fd_char, command, argv2) < 0) {
      printf("ioctl failed with error %d: %s\n", errno, strerror(errno));
    }
    break;
  case IOCTL_REGISTER_EUID:
  case IOCTL_UNREGISTER_EUID:
  case IOCTL_REGISTER_PROG_NAME:
  case IOCTL_UNREGISTER_PROG_NAME:
    if (ioctl(fd_char, command, argv[2]) < 0) {
      printf("ioctl failed with error %d: %s\n", errno, strerror(errno));
    }
    break;
  default:
    printf("argv[1]=%d is outside limits.", argv1);
  }

  close(fd_char);

  return 0;
}
