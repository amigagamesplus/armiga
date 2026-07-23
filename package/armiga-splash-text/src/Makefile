CC      ?= cc
CFLAGS  ?= -O2 -Wall
LDLIBS  ?= -lSDL3 -lSDL3_ttf -lSDL3_image
TARGET   = armiga-splash-text

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) main.c -o $(TARGET) $(LDLIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
