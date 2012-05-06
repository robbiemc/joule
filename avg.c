#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define GREEN "\e[;32m"
#define RED   "\e[;31m"
#define RESET "\e[;0m"

#define NTIMES 5
#define USECS(timeval) ((timeval)->tv_sec * 1000000 + (timeval)->tv_usec)

typedef struct rusage rusage_t;

static void systime(char **argv, rusage_t *usage) {
  int pid, pid2, status;
  pid = fork();
  assert(pid != -1);

  if (pid == 0) {
    close(STDOUT_FILENO);
    /* child */
    execvp(argv[0], argv);
    assert(0);
  }

  pid2 = wait3(&status, 0, usage);
  assert(pid2 == pid);
}

static void avg(char *prog, char *file) {
  rusage_t joule;
  char cmd[1024];

  sprintf(cmd, "luac -o %s.luac %s", file, file);
  system(cmd);
  sprintf(cmd, "%s.luac", file);

  char **argv;
  char *joule_argv[] = {prog, "-c", cmd, NULL};
  char *lua_argv[] = {prog, cmd, NULL};
  argv = strcmp(prog, "lua") == 0 ? lua_argv : joule_argv;

  long total, user, system, mem;
  total = user = system = mem = 0;
  int i;
  for (i = 0; i < NTIMES; i++) {
    systime(argv, &joule);
    total += USECS(&joule.ru_utime) + USECS(&joule.ru_stime);
    user += USECS(&joule.ru_utime);
    system += USECS(&joule.ru_stime);
    mem += joule.ru_maxrss;
  }

  unlink(cmd);

  printf("%27s,", file);
  printf("%10ld,%10ld,%10ld,%10ld\n", total/NTIMES, user/NTIMES, system/NTIMES, mem/NTIMES);
}

int main(int argc, char **argv) {
  int i;

  if (argc < 2) {
    printf("Usage: %s file1 [file2 [file3 ...]]\n", argv[0]);
    return 1;
  }

  printf("%27s%10s%10s%10s%10s\n", "Test File", "total", "user",
         "system", "mem");
  for (i = 2; i < argc; i++) {
    avg(argv[1], argv[i]);
  }
  return 0;
}
