.POSIX:
CC = clang
CFLAGS = -std=c99 -g -Wall -ffast-math -Wno-missing-braces -Wno-pointer-sign -Wno-unused-function
CFLAGS += $(shell pkg-config --cflags epoxy)
LDFLAGS = $(shell pkg-config --libs epoxy) -lm

OBJS = ui.o import.o

all: $(OBJS)

debug: override CFLAGS += -fsanitize=address
debug: override LDFLAGS += -fsanitize=address
debug: all

ui.o: ui.c ui.h
import.o: import.c import.h

clean:
	rm -f *.o
