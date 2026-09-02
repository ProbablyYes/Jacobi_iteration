CC ?= gcc
MPICC ?= $(if $(wildcard .local/openmpi/bin/mpicc),.local/openmpi/bin/mpicc,mpicc)
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion
CPPFLAGS ?= -Iinclude
LDLIBS ?= -lm
BIN_DIR := bin
BUILD_DIR := build

.PHONY: all env test pilot benchmark accuracy plots sanitize clean

env:
	bash scripts/setup_env.sh

all: $(BIN_DIR)/jacobi_serial $(BIN_DIR)/jacobi_mpi_blocking $(BIN_DIR)/jacobi_mpi_overlap

$(BIN_DIR) $(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/common.o: src/common.c include/jacobi.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/serial.o: src/serial.c include/jacobi.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mpi_blocking.o: src/mpi_solver.c include/jacobi.h | $(BUILD_DIR)
	$(MPICC) $(CPPFLAGS) $(CFLAGS) -DJACOBI_OVERLAP=0 -c $< -o $@

$(BUILD_DIR)/mpi_overlap.o: src/mpi_solver.c include/jacobi.h | $(BUILD_DIR)
	$(MPICC) $(CPPFLAGS) $(CFLAGS) -DJACOBI_OVERLAP=1 -c $< -o $@

$(BIN_DIR)/jacobi_serial: $(BUILD_DIR)/serial.o $(BUILD_DIR)/common.o | $(BIN_DIR)
	$(CC) $^ $(LDLIBS) -o $@

$(BIN_DIR)/jacobi_mpi_blocking: $(BUILD_DIR)/mpi_blocking.o $(BUILD_DIR)/common.o | $(BIN_DIR)
	$(MPICC) $^ $(LDLIBS) -o $@

$(BIN_DIR)/jacobi_mpi_overlap: $(BUILD_DIR)/mpi_overlap.o $(BUILD_DIR)/common.o | $(BIN_DIR)
	$(MPICC) $^ $(LDLIBS) -o $@

test: all
	bash tests/run_tests.sh

pilot: all
	bash scripts/pilot.sh

benchmark: all
	bash scripts/benchmark.sh

accuracy: all
	bash scripts/accuracy.sh

plots:
	PYTHONPATH=.local/python$${PYTHONPATH:+:$${PYTHONPATH}} python3 scripts/plot_results.py

sanitize: | $(BIN_DIR)
	$(CC) $(CPPFLAGS) -O1 -g -std=c11 -Wall -Wextra -Wpedantic \
		-fsanitize=address,undefined src/common.c src/serial.c -lm -o $(BIN_DIR)/jacobi_serial_sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BIN_DIR)/jacobi_serial_sanitize --size 32 --fixed-iters 20

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
