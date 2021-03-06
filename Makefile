CC       ?= gcc
CXX      ?= g++
CFLAGS   ?= -Wall -Wextra -Werror -I$(SRCDIR) -Wconversion -g \
            -Wno-unused-parameter
OBJDIR   = objs
SRCDIR   = src
TESTDIR  = tests
CTESTDIR = ctests
BENCHDIR = bench
LDFLAGS  = -lm $(shell llvm-config --libs jit core native) $(shell llvm-config --ldflags) -rdynamic

# Different flags for opt vs debug
ifeq ($(BUILD),opt)
ifeq ($(CC),clang)
CFLAGS += -O4 -DNDEBUG
else
CFLAGS += -O3 -DNDEBUG
endif
else
CFLAGS += -O1
endif

# Order matters in this list because object files listed first have their
# initializers run first, and destructors run last.
OBJS := gc.o lstring.o vm.o opcode.o util.o luav.o parse.o lhash.o debug.o \
	lib/base.o lib/io.o lib/math.o lib/os.o lib/string.o error.o       \
	lib/coroutine.o arch.o lib/table.o llvm.o trace.o
OBJS := $(OBJS:%=$(OBJDIR)/%)

# Eventually this should be all tests, but it's a work in progres...
LUATESTS :=	tail factorial bool closure multipart bool2 math forint	\
		concat loop func fib select math2 bisect cf printf \
		select smallfun nextvar os strings coroutine2 sieve	\
		load pcall metabasic calls noglobals fibfor readonly	\
		echo constructs errors len closure2 closure3	\
		coroutine-gc locals pow not newtable c upvalues while   \
		vararg varsetlist var mult omg-fuck-you-gc small-bench \
		segfault-in-compiled cache
# not passing: cor coroutine literals sort
LUATESTS := $(LUATESTS:%=$(TESTDIR)/%.lua)

BENCHTESTS :=	ackermann.lua-2 ary nbody nbody.lua-2 nbody.lua-4 hash fibo \
		matrix nestedloop nsieve.lua-3 nsievebits random   \
		sieve sieve.lua-2 spectralnorm takfp threadring.lua-3       \
		strcat.lua-2 recursive partialsums.lua-3 partialsums.lua-2  \
		harmonic fannkuchredux fasta fannkuch         \
		fannkuch.lua-2 chameneos hash2 strcat lists \
		objinst                                                     \
		binarytrees.lua-2 binarytrees.lua-3
# not passing: prodcons message.lua-2 methcall except
BENCHTESTS := $(BENCHTESTS:%=$(BENCHDIR)/%.lua)

AVGTESTS := ackermann.lua-2 arith ary binarytrees.lua-2 binarytree.lua-3 \
						chameneos fannkuch fannkuch.lua-2 fannkuchredux fib2 \
						harmonic hash hash2 nbody nestedloop nsieve.lua-3 nsievebits \
						partialsums.lua-2 partialsums.lua-3 random sieve recursive \
						sieve.lua-2 strcat spectralnorm
AVGTESTS := $(AVGTESTS:%=$(BENCHDIR)/%.lua)

.PHONY: bench clean avg

all: joule

joule: $(OBJS) $(OBJDIR)/main.o
	$(CXX) $^ $(CFLAGS) $(LDFLAGS) -o joule

# Run all lua tests
test: $(LUATESTS:=test)
	@echo -- All lua tests passed --
btest: $(BENCHTESTS:=test)
	@echo -- All benchmarks passed --
leaks: $(BENCHTESTS:=leak)
	@echo -- All leak tests passed --
avg: joule avg.c
				$(CC) -o avg avg.c
				./avg ./joule $(AVGTESTS)

bench: benchmark
	./benchmark $(sort $(BENCHTESTS))
benchmark: benchmark.c
	$(CC) -Wall -Wextra -Werror -o $@ $<

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

# Generic targets
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DASSEMBLER -c -o $@ $<

$(OBJDIR)/%.dep: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	gcc $(CFLAGS) -MM -MT $(@:.dep=.o) -MF $@ $<

$(OBJDIR)/llvm.%: CFLAGS += $(shell llvm-config --cflags)

# Running a lua test
%.luatest: joule
	@mkdir -p $(OBJDIR)/$(@D)
	@echo $(@:.luatest=.lua)
	@lua $(@:.luatest=.lua) > $(OBJDIR)/$(@:.luatest=.out)
	@./joule $(@:.luatest=.lua) > $(OBJDIR)/$(@:.luatest=.log)
	@diff -u $(OBJDIR)/$(@:.luatest=.out) $(OBJDIR)/$(@:.luatest=.log)

%.lualeak: joule
	@echo $(@:.lualeak=.lua)
	@grep -q coroutine $(@:.lualeak=.lua) || valgrind --error-exitcode=1 ./joule \
				$(@:.lualeak=.lua)

# If we're cleaning, no need to regenerate all .dep files
ifeq (0,$(words $(filter %clean,$(MAKECMDGOALS))))
-include $(OBJS:.o=.dep)
endif

clean:
	rm -rf $(OBJDIR) joule coverage benchmark
