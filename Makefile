ifeq ($(OS), Windows_NT)
    USER_OS := Windows
else
    USER_OS := $(shell uname -s)
endif

ifneq ($(USER_OS), Darwin)
	ASAN_OPTIONS := detect_leaks=1
endif

RESULT := shell
CFLAGS := -g -fsanitize=address,undefined -Wall -Werror

all: run

run: compile
	ASAN_OPTIONS=$(ASAN_OPTIONS) python3 test.py

compile: $(wildcard *.h)
	gcc $(CFLAGS) main.c -o $(RESULT) -static-libasan

clean:
	rm -f $(RESULT)
