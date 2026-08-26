#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

static void
scan(int fd)
{
  char c;
  int number = 0;
  int reading_number = 0;

  // Parse the file as a stream so numbers can span arbitrary read boundaries.
  while(read(fd, &c, 1) == 1){
    if(c >= '0' && c <= '9'){
      number = number * 10 + c - '0';
      reading_number = 1;
    } else if(reading_number){
      if(number % 5 == 0 || number % 6 == 0)
        printf("%d\n", number);
      number = 0;
      reading_number = 0;
    }
  }

  // A number at end-of-file has no trailing delimiter, so flush it here.
  if(reading_number && (number % 5 == 0 || number % 6 == 0))
    printf("%d\n", number);
}

int
main(int argc, char *argv[])
{
  int fd;

  if(argc < 2){
    fprintf(2, "usage: sixfive file ...\n");
    exit(1);
  }

  for(int i = 1; i < argc; i++){
    if((fd = open(argv[i], O_RDONLY)) < 0){
      fprintf(2, "sixfive: cannot open %s\n", argv[i]);
      continue;
    }
    // Each input file is scanned independently and closed before the next one.
    scan(fd);
    close(fd);
  }

  exit(0);
}
