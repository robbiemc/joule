LUAC     = luac
CC       = gcc
CFLAGS   = -Wall -Wextra -Werror -I$(SRCDIR) -Wconversion -g \
					 -Wno-unused-parameter
OBJDIR   = objs
SRCDIR   = src
TESTDIR  = tests
CTESTDIR = ctests

# Different flags for opt vs debug
ifeq ($(BUILD),opt)
CFLAGS += -O2
else
CFLAGS += -O0
endif

# Order matters in this list because object files listed first have their
# initializers run first, and destructors run last.
OBJS := lstring.o vm.o opcode.o util.o luav.o parse.o lhash.o debug.o \
				lib/utils.o lib/io.o
OBJS := $(OBJS:%=$(OBJDIR)/%)

TESTS := bisect cf echo env factorial fib fibfor globals hello life luac \
				 printf readonly sieve sort table trace-calls trace-globals xd \
				 bool func smallfun multipart closure simplewrite
TESTS := $(TESTS:%=$(TESTDIR)/%.lua)

# Eventually this should be $(TESTS), but we're still a work in progress...
LUATESTS := closure
LUATESTS := $(LUATESTS:%=$(TESTDIR)/%)

CTESTS := hash types parse string
CTESTS := $(CTESTS:%=$(OBJDIR)/$(CTESTDIR)/%)

all: joule

joule: $(OBJS) $(OBJDIR)/main.o
	$(CC) $(CFLAGS) -o joule $^

test: ctest ltest

# Run all compiled tests (C tests)
ctest: ctests
	@for test in $(CTESTS); do \
		echo $$test; \
		$$test > $$test.log || exit 1; \
	done
	@echo -- All C tests passed --

# Run all lua tests
ltest: joule ltests
	@for test in $(LUATESTS); do \
		echo $$test.luac; \
		./joule $$test.luac > $$test.log; \
		diff $$test.log $$test.out; \
	done
	@echo -- All lua tests passed --

# Targets for building all tests
ltests: $(TESTS:.lua=.luac)
ctests: $(CTESTS)

coverage: CFLAGS += --coverage
coverage: clean ctests test
	mkdir -p coverage
	lcov --directory $(OBJDIR) --capture --output-file coverage/app.info -b .
	genhtml --output-directory coverage coverage/app.info
	@rm -f *.gcda *.gcno

profile: CFLAGS += -pg
profile: clean joule ctests

# Generic targets
%.luac: %.lua
	$(LUAC) -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.dep: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	gcc $(CFLAGS) -M -MT $(@:.dep=.o) -MF $@ $<

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
