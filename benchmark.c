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

#define USECS(timeval) ((timeval)->tv_sec * 1000000 + (timeval)->tv_usec)

typedef struct rusage rusage_t;

static void pct(double lua, double joule) {
  double diff = (joule - lua) / lua * 100;
  char *color = diff > 15 ? RED : diff < 0 ? GREEN : RESET;
  printf("%s%9.1f%%%s", color, diff, RESET);
}

static void summarize(char *file, rusage_t *lua, rusage_t *joule) {
  printf("%27s", file);
  pct(USECS(&lua->ru_utime) + USECS(&lua->ru_stime),
      USECS(&joule->ru_utime) + USECS(&joule->ru_stime));
  pct(USECS(&lua->ru_utime), USECS(&joule->ru_utime));
  pct(USECS(&lua->ru_stime), USECS(&joule->ru_stime));
  pct(lua->ru_maxrss, joule->ru_maxrss);
  printf("%8luus", USECS(&joule->ru_utime));
  printf("%8luus", USECS(&joule->ru_stime));
  printf("\n");
}

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

static void benchmark(char *file) {
  rusage_t lua, joule;
  char cmd[1024];

  sprintf(cmd, "luac -o %s.luac %s", file, file);
  system(cmd);
  sprintf(cmd, "%s.luac", file);

  char *lua_argv[] = {"lua", cmd, NULL};
  systime(lua_argv, &lua);

  char *joule_argv[] = {"./joule", "-c", cmd, NULL};
  systime(joule_argv, &joule);

  unlink(cmd);

  summarize(file, &lua, &joule);
}

int main(int argc, char **argv) {
  int i;

  if (argc < 2) {
    printf("Usage: %s file1 [file2 [file3 ...]]\n", argv[0]);
    return 1;
  }

  printf("%27s%10s%10s%10s%10s%10s\n", "Test File", "total", "user",
         "system", "mem", "joule");
  for (i = 1; i < argc; i++) {
    benchmark(argv[i]);
  }
  return 0;
}
