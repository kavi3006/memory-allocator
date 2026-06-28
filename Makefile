CC      := gcc
CFLAGS  := -g -Wall -Wextra -std=gnu11 -I.
LDFLAGS := -lpthread

SRCS    := $(wildcard *.c)
HDRS    := $(wildcard *.h)

.PHONY: all tests test format format-check lint clean

all: tests

tests:
	$(MAKE) -C tests

test: tests
	python3 ./runtest.py

format:
	clang-format -i --style=file $(SRCS) $(HDRS)

format-check:
	clang-format --dry-run --Werror --style=file $(SRCS) $(HDRS)

lint:
	clang-tidy $(SRCS) -- $(CFLAGS)

clean:
	$(MAKE) -C tests clean
