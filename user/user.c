#define _XOPEN_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/types.h> // Header for pid_t
#include <unistd.h>

int main(int a, char **b) {

  pid_t process_id = getpid();
  pid_t parent_id = getppid();

  printf("Current Process ID (PID): %d\n", process_id);
  printf("Parent Process ID (PPID): %d\n", parent_id);

  char buff[64];
  printf("address is %p\n", buff);

  while (1) {
    write(1, buff, read(0, buff, 64));
  }

  return 0;
}
