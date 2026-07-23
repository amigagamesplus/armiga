CC      ?= cc
CFLAGS  ?= -O2 -Wall
TARGET   = armiga-splash-text

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) $(Z_CFLAGS) main.c -o $(TARGET) $(Z_LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
