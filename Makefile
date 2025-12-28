CC = gcc
CFLAGS = -Wextra -Wall
TARGET = chat_server

all: $(TARGET)

$(TARGET): server.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean