LUAC    = luac
CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -I$(SRCDIR)
DFLAGS  = -g -O0
OFLAGS  = -O2
OBJDIR  = objs
SRCDIR  = src
TESTDIR = tests

OBJS := main.o parse.o
OBJS := $(OBJS:%=$(OBJDIR)/%)

TESTS := bisect cf echo env factorial fib fibfor globals hello life luac \
				 printf readonly sieve sort table trace-calls trace-globals xd
TESTS := $(TESTS:%=$(TESTDIR)/%.lua)

all: debug

debug: CFLAGS += $(DFLAGS)
debug: joule
opt: CFLAGS += $(OFLAGS)
opt: joule

joule: $(OBJS)
	$(CC) $(CFLAGS) -o joule $^

tests: $(TESTS:.lua=.luac)

%.luac: %.lua
	$(LUAC) -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(TESTDIR)/*.luac $(OBJDIR) joule
