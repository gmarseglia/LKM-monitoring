#include <bits/time.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define WRITE_ITER 1000 * 1000
#define ITER_NUM 50

int main(int a, char **b) {

  pid_t process_id = getpid();
  pid_t parent_id = getppid();
  uid_t euid = geteuid();

  int fd_null = open("/dev/null", O_WRONLY);

  if (fd_null == -1) {
    perror("Failed to open /dev/null");
    return 1;
  }

  printf("PID : %d\n", process_id);
  printf("PPID: %d\n", parent_id);
  printf("eUID: %u\n\n", euid);

  char buff[64];

  struct timespec start, end;
  double mean_ms;

  double elapsed_vec[ITER_NUM];

  for (unsigned int i = 0; i < ITER_NUM; i++) {
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int j = 0; j < WRITE_ITER; j++) {
      write(fd_null, "", 1);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_ms = (double)(end.tv_sec - start.tv_sec) * 1000.0 +
                        (double)(end.tv_nsec - start.tv_nsec) / 1000000.0;

    elapsed_vec[i] = elapsed_ms;

    printf("writes=%d, iteration=%03d/%03d, elapsed=%0.3f msec.\n", WRITE_ITER,
           (i+1), ITER_NUM, elapsed_ms);
  }

  close(fd_null);

  double final_mean_ms = 0;
  for (int i = 0; i < ITER_NUM; i++) {
    final_mean_ms += elapsed_vec[i];
  }
  final_mean_ms /= ITER_NUM;

  double final_std_ms = 0;
  for (int i = 0; i < ITER_NUM; i++) {
    double diff = elapsed_vec[i] - final_mean_ms;
    final_std_ms += diff * diff;
  }
  final_std_ms = sqrt(final_std_ms / ITER_NUM);

  printf("total=%.3f sec, mean=%.3f msec, stddev=%.3f msec\n",
         (final_mean_ms * ITER_NUM / 1000), final_mean_ms, final_std_ms);

  return 0;
}
