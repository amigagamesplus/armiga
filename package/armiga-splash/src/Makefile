CC      ?= gcc
CFLAGS  ?= -O2
Z_CFLAGS ?=
Z_LIBS   ?= -lz
TARGET   = armiga-splash

$(TARGET): main.c stb_image.h
	$(CC) $(CFLAGS) $(Z_CFLAGS) -o $@ main.c $(Z_LIBS) -lm

clean:
	rm -f $(TARGET)
