CC = gcc

CFLAGS = -Wall -Wextra -std=c99
LDFLAGS = -lpthread -lm -ldl

TARGET = tcb

SRC = tcb.c

OBJ = $(SRC:.c=.o)

INCLUDES = -I../

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)

.PHONY: all clean install uninstall

install: $(TARGET)
	mkdir -p /usr/local/bin
	cp $(TARGET) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(TARGET)