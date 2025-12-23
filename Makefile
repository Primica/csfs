CC = cc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -Iinclude
LDFLAGS =

SRCS := $(wildcard src/*.c) $(wildcard src/man/*.c) $(wildcard src/fetch/*.c) $(wildcard src/editor/*.c) $(wildcard src/completion/*.c)
OBJS := $(SRCS:src/%.c=build/%.o)
BIN := csfs

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

build:
	@mkdir -p build build/man build/fetch build/editor build/completion

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/man/%.o: src/man/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/fetch/%.o: src/fetch/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/editor/%.o: src/editor/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/completion/%.o: src/completion/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build $(BIN)
