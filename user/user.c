#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define SLEEP_MSEC 10
#define SEC_PER_SLEEP (1000 / SLEEP_MSEC)
#define WRITE_ITER SEC_PER_SLEEP

void msleep(long ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000;
  nanosleep(&ts, NULL);
}

int main(int a, char **b) {

  pid_t process_id = getpid();
  pid_t parent_id = getppid();
  uid_t euid = geteuid();

  int fd_null = open("/dev/null", O_WRONLY);

  if (fd_null == -1) {
    perror("Failed to open /dev/null");
    return 1;
  }

  char buff[64];
  printf("address is %p\n", buff);

  for (unsigned int i = 0;; i++) {
    printf("PID : %d\n", process_id);
    printf("PPID: %d\n", parent_id);
    printf("eUID: %u\n", euid);

    for (int j = 0; j < WRITE_ITER; j++) {
      int n = printf(buff, "%d", j);
      write(fd_null, buff, n);
      msleep(10);
    }
    printf("Done %d writes for the %dth time.\n\n", WRITE_ITER, i);
  }

  close(fd_null);

  return 0;
}
