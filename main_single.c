#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#endif

#include "src/instructions.h"
#include "src/memory.h"

#define REG_COUNT 32

uint32_t pc = 0;
uint32_t x[REG_COUNT];
uint32_t imm = 0;
uint32_t instruction_count = 0;
#define MAX_INSTRUCTIONS 100000

bool running = true;

void set_pc(uint32_t value)  { pc = value; }
uint32_t get_pc()            { return pc; }

void set_register(uint32_t reg, uint32_t value) {
    if (reg == 0) return;
    if (reg < REG_COUNT) x[reg] = value;
}

uint32_t get_register(uint32_t reg) {
    return (reg < REG_COUNT) ? x[reg] : 0;
}

bool execute_instruction() {
    if (instruction_count >= MAX_INSTRUCTIONS) return false;
    if (pc >= MAX_MEMORY) return false;

    instruction_count++;
    uint32_t instruction = get_memory(pc);
    pc += 4;

    if (instruction == 0x73) return false;

    dispatch_type(instruction);
    return true;
}

int main(int argc, char *argv[]) {
    bool json_mode = false;

    if (argc >= 3 && strcmp(argv[2], "--json") == 0)
        json_mode = true;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file_path> [--json]\n", argv[0]);
        return EXIT_FAILURE;
    }

    memset(x, 0, sizeof(x));
    pc = 0;
    instruction_count = 0;

    uint32_t loaded = load_file(argv[1], memory);
    if (loaded == 0xFFFFFFFF) return EXIT_FAILURE;

    while (running) {
        if (memory_full) break;
        running = execute_instruction();
    }

    bin_dump_registers(x, REG_COUNT, argv[1]);

    if (json_mode) {
        /* Single-cycle: cycles == instructions, CPI == 1.0 */
        fprintf(stdout,
            "{\"instructions\":%u,\"cycles\":%u,\"cpi\":1.00}\n",
            instruction_count, instruction_count);
    }

    return 0;
}
