CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -D_POSIX_C_SOURCE=200809L
LDLIBS = 
TARGET = ipk25chat-client

SRCDIR = src

SOURCES = \
  $(SRCDIR)/main.c \
  $(SRCDIR)/client.c \
  $(SRCDIR)/tcp.c \
  $(SRCDIR)/udp.c \
  $(SRCDIR)/utils.c \

OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
