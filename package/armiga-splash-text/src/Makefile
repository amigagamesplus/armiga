CC      ?= cc
CFLAGS  ?= -O2 -Wall
TARGET   = armiga-splash-text

all: $(TARGET)

$(TARGET): main.c font6x10.h
	$(CC) $(CFLAGS) main.c -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
