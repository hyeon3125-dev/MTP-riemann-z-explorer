CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200112L
LIBS    = -lm -lrt
TARGET  = riemann_explorer
SRC     = riemann_explorer.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

# Quick smoke-test: t_max=100, dt=0.01
run: $(TARGET)
	./$(TARGET) 100.0 0.01

# Extended run: t_max=1000, dt=0.005
run-extended: $(TARGET)
	./$(TARGET) 1000.0 0.005

clean:
	rm -f $(TARGET)
