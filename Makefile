CC = cc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -Iinclude
LDFLAGS =

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=build/%.o)
BIN := csfs

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

build:
	@mkdir -p build

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build $(BIN)
