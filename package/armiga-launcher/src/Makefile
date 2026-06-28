CC ?= gcc
CFLAGS ?= -O2
LDFLAGS ?=

SDL3_CFLAGS := $(shell pkg-config --cflags sdl3 SDL3_ttf)
SDL3_LIBS   := $(shell pkg-config --libs sdl3 SDL3_ttf)

CFLAGS += $(SDL3_CFLAGS)
LDFLAGS += $(SDL3_LIBS)

TARGET = armiga-launcher
SRCS = main.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)
