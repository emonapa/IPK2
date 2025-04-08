CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
LDLIBS =
TARGET = ipk25chat-client

SRCDIR = src

SOURCES = \
  $(SRCDIR)/main.c \
  $(SRCDIR)/client.c \
  $(SRCDIR)/tcp.c \
  $(SRCDIR)/udp.c \

OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
