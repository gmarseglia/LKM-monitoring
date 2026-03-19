#define _XOPEN_SOURCE
#define _POSIX_C_SOURCE 199309L
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h> // Header for pid_t
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define DEVICE_PATH "/dev/sys_thr"

void msleep(long ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000;
  nanosleep(&ts, NULL);
}

int main(int a, char **b) {

  pid_t process_id = getpid();
  pid_t parent_id = getppid();

  printf("Current Process ID (PID): %d\n", process_id);
  printf("Parent Process ID (PPID): %d\n", parent_id);

  int fd_char = open(DEVICE_PATH, O_RDONLY);

  if (fd_char < 0) {
    perror("Failed to open DEVICE_PATH");
    return 1;
  }

  ioctl(fd_char, 1, 100);

  close(fd_char);

  return 0;
}
