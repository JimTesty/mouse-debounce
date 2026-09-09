CC ?= clang
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
FRAMEWORKS = -framework ApplicationServices -framework CoreFoundation

all: mouse-debounce

mouse-debounce: mouse_debounce.c
	$(CC) $(CFLAGS) mouse_debounce.c -o $@ $(FRAMEWORKS)

clean:
	rm -f mouse-debounce

.PHONY: all clean
