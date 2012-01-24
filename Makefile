LUAC     = luac
CC       = gcc
CFLAGS   = -Wall -Wextra -Werror -I$(SRCDIR) -Wconversion
DFLAGS   = -g -O0
OFLAGS   = -O2
OBJDIR   = objs
SRCDIR   = src
TESTDIR  = tests
CTESTDIR = ctests

OBJS := opcode.o util.o luav.o parse.o vm.o lhash.o
OBJS := $(OBJS:%=$(OBJDIR)/%)

TESTS := bisect cf echo env factorial fib fibfor globals hello life luac \
				 printf readonly sieve sort table trace-calls trace-globals xd \
				 bool func
TESTS := $(TESTS:%=$(TESTDIR)/%.lua)

CTESTS := hash
CTESTS := $(CTESTS:%=$(CTESTDIR)/%)

all: debug

debug: CFLAGS += $(DFLAGS)
debug: joule
opt: CFLAGS += $(OFLAGS)
opt: joule

joule: $(OBJS) $(OBJDIR)/main.o
	$(CC) $(CFLAGS) -o joule $^

tests: $(TESTS:.lua=.luac) $(CTESTS)

%.luac: %.lua
	$(LUAC) -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.dep: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -M -MT $(@:.dep=.o) -MF $@ $<

$(CTESTS): %: $(OBJS) %.c
	$(CC) $(CFLAGS) -O1 -g -o $@ $^

# If we're cleaning, no need to regenerate all .dep files
ifeq (0,$(words $(filter %clean,$(MAKECMDGOALS))))
-include $(OBJS:.o=.dep)
endif

clean:
	rm -rf $(TESTDIR)/*.luac $(OBJDIR) joule $(CTESTS)
