CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
COMMON = process.c memory.c policy.c metrics.c

all: simulator
simulator: main.c $(COMMON)
	$(CC) $(CFLAGS) main.c $(COMMON) -o simulator
tests: tests.c process.c memory.c policy.c metrics.c
	$(CC) $(CFLAGS) tests.c process.c memory.c policy.c metrics.c -o tests
test: tests
	./tests
clean:
	rm -f simulator simulator.exe tests tests.exe events.log events_*.log
.PHONY: all tests test clean
