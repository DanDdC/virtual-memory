CC = gcc
CPPFLAGS ?=
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2 -pthread
TARGET = vm

all: $(TARGET)

$(TARGET): vm.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $(TARGET) vm.c

clean:
	rm -f $(TARGET) correct.txt.tmp

.PHONY: all clean
