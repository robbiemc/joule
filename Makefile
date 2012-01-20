TESTS := bisect cf echo env factorial fib fibfor globals hello life luac \
				 printf readonly sieve sort table trace-calls trace-globals xd
TESTS := $(TESTS:%=tests/%.lua)

LUAC = luac

all: $(TESTS:.lua=.luac)

%.luac: %.lua
	$(LUAC) -o $@ $<

clean:
	rm $(TESTS:.lua=.luac)
