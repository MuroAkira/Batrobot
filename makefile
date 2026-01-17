CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -D_DEFAULT_SOURCE -Iinclude
TARGET := build/thermophone

SRCS := $(wildcard src/*.c)
OBJS := $(patsubst src/%.c,build/%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS) | build
	$(CC) $(OBJS) -o $@ -lfftw3f -lm

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@ -lm

build:
	mkdir -p build

clean:
	rm -f build/*.o $(TARGET)
