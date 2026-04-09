#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define ITER (1000 * 1000)

// Comparison function required by qsort for sorting doubles
int compare_doubles(const void *a, const void *b) {
  double arg1 = *(const double *)a;
  double arg2 = *(const double *)b;
  if (arg1 < arg2)
    return -1;
  if (arg1 > arg2)
    return 1;
  return 0;
}

int main(int argc, char **argv) {

  int fd_null = open("/dev/null", O_RDWR);

  if (fd_null == -1) {
    perror("Failed to open /dev/null");
    return 1;
  }

  char buff[64];
  struct timespec start, end;

  // Allocate the large array on the heap
  double *elapsed_ns_vec = malloc(ITER * sizeof(double));
  if (elapsed_ns_vec == NULL) {
    perror("Failed to allocate memory");
    close(fd_null);
    return 1;
  }

  for (unsigned int i = 0; i < ITER; i++) {
    clock_gettime(CLOCK_MONOTONIC, &start);

    read(fd_null, &buff, sizeof(buff));

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_ns =
        (double)(end.tv_sec - start.tv_sec) * 1000.0 * 1000.0 * 1000.0 +
        (double)(end.tv_nsec - start.tv_nsec);

    elapsed_ns_vec[i] = elapsed_ns;

    if ((i + 1) % (ITER / 10) == 0) {
      printf("iteration=%07d/%07d\n", (i + 1), ITER);
    }
  }

  close(fd_null);

  printf("\nSorting %d data points...\n", ITER);

  // Sort the array from lowest to highest latency
  qsort(elapsed_ns_vec, ITER, sizeof(double), compare_doubles);

  // Calculate Percentiles
  double p50 = elapsed_ns_vec[ITER / 2];              // Median (50%)
  double p90 = elapsed_ns_vec[(int)(ITER * 0.90)];    // 90th percentile
  double p99 = elapsed_ns_vec[(int)(ITER * 0.99)];    // 99th percentile
  double p99_9 = elapsed_ns_vec[(int)(ITER * 0.999)]; // 99.9th percentile
  double p100 = elapsed_ns_vec[ITER - 1];             // Max value

  printf("\n--- Latency Percentiles ---\n");
  printf("p50 (Median) : %.3f nsec\n", p50);
  printf("p90          : %.3f nsec\n", p90);
  printf("p99          : %.3f nsec\n", p99);
  printf("p99.9        : %.3f nsec\n", p99_9);
  printf("p100 (Max)   : %.3f nsec\n", p100);

  // Clean up heap memory
  free(elapsed_ns_vec);

  return 0;
}