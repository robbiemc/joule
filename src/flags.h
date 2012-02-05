#ifndef _FLAGS_H_
#define _FLAGS_H_

typedef struct lflags {
  char  dump;
  char  compiled;
  char  string;
  char  print;
} lflags_t;

extern lflags_t flags;

#endif /* _FLAGS_H_ */
