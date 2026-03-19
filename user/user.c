#define _XOPEN_SOURCE
#define _POSIX_C_SOURCE 199309L
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h> // Header for pid_t
#include <time.h>
#include <unistd.h>

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

  int fd_null = open("/dev/null", O_WRONLY);

  if (fd_null == -1) {
    perror("Failed to open /dev/null");
    return 1;
  }

  char buff[64];
  printf("address is %p\n", buff);

  // while (1) {
  //   write(1, buff, read(0, buff, 64));
  // }

  // while (1) {
  //   read(0, buff, 64); // just to wait user input

  //   for (int i = 0; i < 8; i++) {
  //     sprintf(buff, "%d", i);
  //     write(1, buff, strlen(buff));
  //   }
  // }

  for (int i = 0;; i++) {
    int n = printf(buff, "%d", i);
    write(fd_null, buff, n);
    msleep(10);
  }

  close(fd_null);

  return 0;
}
