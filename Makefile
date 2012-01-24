LUAC     = luac
CC       = gcc
CFLAGS   = -Wall -Wextra -Werror -I$(SRCDIR) -Wconversion
OBJDIR   = objs
SRCDIR   = src
TESTDIR  = tests
CTESTDIR = ctests

# Different flags for opt vs debug
ifeq (0,$(words $(filter %opt,$(MAKECMDGOALS))))
CFLAGS += -g -O0
else
CFLAGS += -O2
endif

OBJS := opcode.o util.o luav.o parse.o vm.o lhash.o lstring.o
OBJS := $(OBJS:%=$(OBJDIR)/%)

TESTS := bisect cf echo env factorial fib fibfor globals hello life luac \
				 printf readonly sieve sort table trace-calls trace-globals xd \
				 bool func
TESTS := $(TESTS:%=$(TESTDIR)/%.lua)

CTESTS := hash types parse string
CTESTS := $(CTESTS:%=$(OBJDIR)/$(CTESTDIR)/%)

all: joule

joule: $(OBJS) $(OBJDIR)/main.o
	$(CC) $(CFLAGS) -o joule $^

# Run all of the C-tests (compiled) for now, eventually run lua tests
test: ctests
	@for test in $(CTESTS); do \
		echo $$test;\
		$$test;     \
	done
	@echo -- All tests passed --

# Targets for building all tests
ltests: $(TESTS:.lua=.luac)
ctests: $(CTESTS)

coverage: CFLAGS += --coverage
coverage: clean ctests test
	mkdir -p coverage
	lcov --directory $(OBJDIR) --capture --output-file coverage/app.info -b .
	genhtml --output-directory coverage coverage/app.info
	@rm -f *.gcda *.gcno

# Generic targets
%.luac: %.lua
	$(LUAC) -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.dep: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -M -MT $(@:.dep=.o) -MF $@ $<

# Target for test executables
$(OBJDIR)/%: $(OBJS) %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^

# If we're cleaning, no need to regenerate all .dep files
ifeq (0,$(words $(filter %clean,$(MAKECMDGOALS))))
-include $(OBJS:.o=.dep)
endif

clean:
	rm -rf $(TESTDIR)/*.luac $(OBJDIR) joule coverage
