CC ?= gcc
CFLAGS ?= -O2
LDFLAGS ?=
TARGET = armiga-launcher
SRCS = main.c
all: $(TARGET)
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
clean:
	rm -f $(TARGET)
