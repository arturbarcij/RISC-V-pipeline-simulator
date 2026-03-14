# RISC-V 5-Stage Pipeline Simulator

CC = gcc
CFLAGS = -Wall -g

SRC = main.c src/memory.c src/pipeline.c
OUTPUT = program

all: $(OUTPUT)

$(OUTPUT): $(SRC) src/pipeline.h src/memory.h
	$(CC) $(CFLAGS) -o $(OUTPUT) $(SRC)

checker: result_checker.c
	$(CC) $(CFLAGS) -o checker result_checker.c

clean:
	rm -f $(OUTPUT) checker

run: all
	./$(OUTPUT)

test: all checker
	@for f in test_files/T1/*.bin test_files/T2/*.bin test_files/T3/*.bin test_files/T4/*.bin; do \
		./$(OUTPUT) "$$f" > /dev/null 2>&1; \
	done
	./checker
