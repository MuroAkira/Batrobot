CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -D_DEFAULT_SOURCE -Iinclude
TARGET := build/thermophone

SRCS := $(wildcard src/*.c)
OBJS := $(patsubst src/%.c,build/%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS) | build
	$(CC) $(OBJS) -o $@

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -f build/*.o $(TARGET)
