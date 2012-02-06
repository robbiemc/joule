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
CFLAGS += -O3 -DNDEBUG
endif
else
CFLAGS += -O0
endif

# Order matters in this list because object files listed first have their
# initializers run first, and destructors run last.
OBJS := lstring.o vm.o opcode.o util.o luav.o parse.o lhash.o debug.o \
	lib/base.o lib/io.o lib/math.o lib/os.o lib/string.o error.o \
	lib/coroutine.o lib/co_asm.o lib/table.o
OBJS := $(OBJS:%=$(OBJDIR)/%)

# Eventually this should be all tests, but it's a work in progres...
LUATESTS := tail factorial bool closure multipart bool2 math forint concat \
	    loop sort func fib select math2 bisect cf printf select smallfun \
            os strings coroutine2 sieve load pcall metabasic calls coroutine \
	    noglobals fibfor readonly echo
LUATESTS := $(LUATESTS:%=$(TESTDIR)/%.lua)

BENCHTESTS := ackermann.lua-2 ary binarytrees.lua-2 nbody nbody.lua-2 \
	      nbody.lua-4 hash fibo matrix nestedloop nsieve.lua-3 \
              nsievebits prodcons random sieve sieve.lua-2 spectralnorm takfp \
	      threadring.lua-3 strcat.lua-2 recursive process \
	      partialsums.lua-3 partialsums.lua-2 message.lua-2 harmonic \
	      fannkuchredux fasta fannkuch fannkuch.lua-2 chameneos \
	      binarytrees.lua-3
BENCHTESTS := $(BENCHTESTS:%=$(BENCHDIR)/%.lua)

CTESTS := hash types
CTESTS := $(CTESTS:%=$(OBJDIR)/$(CTESTDIR)/%)

.PHONY: bench clean

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
ltest: $(LUATESTS:=test)
	@echo -- All lua tests passed --

# Run all benchmark tests
btest: $(BENCHTESTS:=test)
	@echo -- All benchmark tests passed --

bench: joule
	./bench.rb $(BENCHTESTS)

lmissing:
	@ruby -e 'puts `ls $(TESTDIR)/*.lua`.split("\n") - ARGV' $(LUATESTS)
bmissing:
	@ruby -e 'puts `ls $(BENCHDIR)/*.lua`.split("\n") - ARGV' $(BENCHTESTS)

coverage: CFLAGS += --coverage
coverage: clean test
	mkdir -p coverage
	lcov --directory $(OBJDIR) --capture --output-file coverage/app.info -b .
	genhtml --output-directory coverage coverage/app.info
	@rm -f *.gcda *.gcno

profile: CFLAGS += -pg -DHASH_PROFILE
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

# Running a lua test
%.luatest:
	@mkdir -p $(OBJDIR)/$(@D)
	@echo $(@:.luatest=.lua)
	@lua $(@:.luatest=.lua) > $(OBJDIR)/$(@:.luatest=.out)
	@./joule $(@:.luatest=.lua) > $(OBJDIR)/$(@:.luatest=.log)
	@diff -u $(OBJDIR)/$(@:.luatest=.out) $(OBJDIR)/$(@:.luatest=.log)

# If we're cleaning, no need to regenerate all .dep files
ifeq (0,$(words $(filter %clean,$(MAKECMDGOALS))))
-include $(OBJS:.o=.dep)
endif

clean:
	rm -rf $(OBJDIR) joule coverage
