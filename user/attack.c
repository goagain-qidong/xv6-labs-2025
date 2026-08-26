#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

static int
valid_secret(char *s)
{
  int n = 0;

  while(n < 64 && s[n] != 0){
    char c = s[n];
    if(!((c >= '0' && c <= '9') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z')))
      return 0;
    n++;
  }

  return n > 0 && n < 64;
}

int
main(int argc, char *argv[])
{
  char *start;
  char *end;
  int bytes = 32 * PGSIZE;

  start = sbrk(bytes);
  if(start == SBRK_ERROR)
    exit(1);
  end = start + bytes;

  // 代码页里也可能有提示文字,只取后面像秘密的内容
  for(char *p = start; p + 32 < end; p++){
    if(memcmp(p, "This may help.", 14) == 0 && valid_secret(p + 16)){
      printf("%s\n", p + 16);
      exit(0);
    }
  }

  exit(1);
}
