#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define SLEEP_MSEC 10
#define SEC_PER_SLEEP (1000 / SLEEP_MSEC)
#define MAX_SIGINT 3

void msleep(long ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000;
  nanosleep(&ts, NULL);
}

int sigint_received = 0;
int fd_null;

void handle_sigint(int sig) {
  if (++sigint_received > MAX_SIGINT) {
    printf("\nExiting.\n");
    close(fd_null);
    exit(0);
  } else {
    printf(
        "\nCaught signal %d (CTRL+C). %d/%d times -> resuming operation...\n",
        sig, sigint_received, MAX_SIGINT);
  }
}

int main(int a, char **b) {

  int ret;
  unsigned int i = 0;
  struct sigaction sa;

  // Setup the sigaction struct
  sa.sa_handler = &handle_sigint;
  sa.sa_flags = SA_RESTART;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("Error registering sigaction");
    return 1;
  }

  fd_null = open("/dev/null", O_RDWR);
  if (fd_null == -1) {
    perror("Failed to open /dev/null");
    return 1;
  }

  char buff[64];

  printf("Starting read iterations\n\n");
  while (1) {
    ret = read(fd_null, buff, sizeof(buff));
    if (ret < 0) {
      perror("read failed");
      continue;
    }

    printf("read completed for the %dth time.\n", i);
    msleep(10);
    i++;
  }

  return 0;
}
