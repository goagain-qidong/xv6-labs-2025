#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"
#include "user/user.h"

static char *
basename(char *path)
{
  char *name = path + strlen(path);

  while(name > path && name[-1] != '/')
    name--;
  return name;
}

static void
run_command(char **command, int command_argc, char *path)
{
  char *args[MAXARG];
  int pid;

  if(command_argc + 1 >= MAXARG){
    fprintf(2, "find: too many arguments for -exec\n");
    return;
  }

  for(int i = 0; i < command_argc; i++)
    args[i] = command[i];
  // Append the matched path, then terminate argv as exec() requires.
  args[command_argc] = path;
  args[command_argc + 1] = 0;

  if((pid = fork()) < 0){
    fprintf(2, "find: fork failed\n");
    return;
  }
  if(pid == 0){
    exec(command[0], args);
    fprintf(2, "find: exec %s failed\n", command[0]);
    exit(1);
  }
  wait(0);
}

static void
find(char *path, char *target, char **command, int command_argc)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }
  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  if(st.type == T_FILE || st.type == T_DEVICE){
    if(strcmp(basename(path), target) == 0){
      if(command_argc == 0)
        printf("%s\n", path);
      else
        run_command(command, command_argc, path);
    }
    close(fd);
    return;
  }

  if(st.type != T_DIR){
    close(fd);
    return;
  }

  if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
    fprintf(2, "find: path too long\n");
    close(fd);
    return;
  }

  strcpy(buf, path);
  p = buf + strlen(buf);
  *p++ = '/';
  // Directories contain fixed-size dirent records rather than C strings.
  while(read(fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == 0)
      continue;
    memmove(p, de.name, DIRSIZ);
    p[DIRSIZ] = '\0';
    // Skipping these entries prevents recursion cycles.
    if(strcmp(p, ".") == 0 || strcmp(p, "..") == 0)
      continue;
    find(buf, target, command, command_argc);
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int exec_index = 0;

  if(argc < 3){
    fprintf(2, "usage: find path name [-exec command ...]\n");
    exit(1);
  }

  if(argc > 3){
    if(strcmp(argv[3], "-exec") != 0 || argc == 4){
      fprintf(2, "usage: find path name [-exec command ...]\n");
      exit(1);
    }
    exec_index = 4;
  }

  find(argv[1], argv[2], exec_index ? &argv[exec_index] : 0,
       exec_index ? argc - exec_index : 0);
  exit(0);
}
