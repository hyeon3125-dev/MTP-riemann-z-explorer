CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200112L
LIBS_RE = -lm -lrt
LIBS_TC = -lm

.PHONY: all clean run run-extended run-tc

all: riemann_explorer thermal_controller

riemann_explorer: riemann_explorer.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS_RE)

thermal_controller: thermal_controller.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS_TC)

# riemann_explorer: quick smoke-test
run: riemann_explorer
	./riemann_explorer 100.0 0.01

# riemann_explorer: extended scan
run-extended: riemann_explorer
	./riemann_explorer 1000.0 0.005

# thermal_controller: all three scenarios
run-tc: thermal_controller
	./thermal_controller

clean:
	rm -f riemann_explorer thermal_controller
