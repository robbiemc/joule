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

# Eventually this should be all tests, but it's a work in progres...
LUATESTS := closure multipart
LUATESTS := $(LUATESTS:%=$(TESTDIR)/%)

CTESTS := hash types parse string
CTESTS := $(CTESTS:%=$(OBJDIR)/$(CTESTDIR)/%)

all: joule

joule: $(OBJS) $(OBJDIR)/main.o
	$(CC) $(CFLAGS) -o joule $^

test: ctest ltest
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
	@mkdir -p $(OBJDIR)/tests
	@for test in $(LUATESTS); do \
		echo $$test.lua; \
		luac -o $(OBJDIR)/$$test.luac $$test.lua; \
		lua $$test.lua > $(OBJDIR)/$$test.out; \
		./joule $(OBJDIR)/$$test.luac > $(OBJDIR)/$$test.log; \
		diff $(OBJDIR)/$$test.log $(OBJDIR)/$$test.out; \
	done
	@echo -- All lua tests passed --

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
	rm -rf $(OBJDIR) joule coverage
