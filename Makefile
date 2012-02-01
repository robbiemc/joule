CC       = gcc
CFLAGS   = -Wall -Wextra -Werror -I$(SRCDIR) -Wconversion -g -m64 \
					 -Wno-unused-parameter
OBJDIR   = objs
SRCDIR   = src
TESTDIR  = tests
CTESTDIR = ctests
BENCHDIR = bench

# Different flags for opt vs debug
ifeq ($(BUILD),opt)
ifeq ($(CC),clang)
CFLAGS += -O4 -DNDEBUG
else
CFLAGS += -O2 -DNDEBUG
endif
else
CFLAGS += -O0
endif

# Order matters in this list because object files listed first have their
# initializers run first, and destructors run last.
OBJS := lstring.o vm.o opcode.o util.o luav.o parse.o lhash.o debug.o \
				lib/base.o lib/io.o lib/math.o lib/os.o lib/string.o error.o \
				lib/coroutine.o lib/co_asm.o
OBJS := $(OBJS:%=$(OBJDIR)/%)

# Eventually this should be all tests, but it's a work in progres...
LUATESTS := tail factorial bool closure multipart bool2 math forint concat \
	    			loop sort func fib select math2 bisect cf printf select smallfun \
            os strings coroutine2 sieve load
LUATESTS := $(LUATESTS:%=$(TESTDIR)/%)

BENCHTESTS := ackermann.lua-2 ary binarytrees.lua-2 nbody nbody.lua-2 \
	      			nbody.lua-4 hash hello fibo matrix nestedloop nsieve.lua-3 \
              nsievebits prodcons random sieve sieve.lua-2 spectralnorm
BENCHTESTS := $(BENCHTESTS:%=$(BENCHDIR)/%)

CTESTS := hash types parse
CTESTS := $(CTESTS:%=$(OBJDIR)/$(CTESTDIR)/%)

all: joule

joule: $(OBJS) $(OBJDIR)/main.o
	$(CC) $(CFLAGS) -o joule $^ -lm

test: ctest ltest btest
ctests: $(CTESTS)

# Run all compiled tests (C tests)
ctest: ctests
	@for test in $(CTESTS); do \
		echo $$test; \
		$$test > $$test.log || exit 1; \
	done
	@echo -- All C tests passed --

# Run all lua tests
ltest: joule
	@mkdir -p $(OBJDIR)/$(TESTDIR)
	@for test in $(LUATESTS); do \
		echo $$test.lua; \
		lua $$test.lua > $(OBJDIR)/$$test.out; \
		./joule $$test.lua > $(OBJDIR)/$$test.log || exit 1; \
		diff -u $(OBJDIR)/$$test.out $(OBJDIR)/$$test.log || exit 1; \
	done
	@echo -- All lua tests passed --

# Run all benchmark tests
btest: joule
	@mkdir -p $(OBJDIR)/$(BENCHDIR)
	@for test in $(BENCHTESTS); do \
		echo $$test.lua; \
		lua $$test.lua > $(OBJDIR)/$$test.out; \
		./joule $$test.lua > $(OBJDIR)/$$test.log || exit 1; \
		diff -u $(OBJDIR)/$$test.out $(OBJDIR)/$$test.log || exit 1; \
	done
	@echo -- All bench tests passed --

coverage: CFLAGS += --coverage
coverage: clean test
	mkdir -p coverage
	lcov --directory $(OBJDIR) --capture --output-file coverage/app.info -b .
	genhtml --output-directory coverage coverage/app.info
	@rm -f *.gcda *.gcno

profile: CFLAGS += -pg
profile: clean joule ctests

# Generic targets
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DASSEMBLER -c -o $@ $<

$(OBJDIR)/%.dep: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	gcc $(CFLAGS) -M -MT $(@:.dep=.o) -MF $@ $<

# Target for test executables
$(OBJDIR)/%: $(OBJS) %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ -lm

# If we're cleaning, no need to regenerate all .dep files
ifeq (0,$(words $(filter %clean,$(MAKECMDGOALS))))
-include $(OBJS:.o=.dep)
endif

clean:
	rm -rf $(OBJDIR) joule coverage
