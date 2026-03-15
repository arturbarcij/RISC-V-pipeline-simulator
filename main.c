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

#include "src/pipeline.h"
#include "src/memory.h"

#define REG_COUNT 32

uint32_t pc = 0;
uint32_t x[REG_COUNT];

int main(int argc, char *argv[]) {
    bool json_mode = false;

    if (argc >= 3 && strcmp(argv[2], "--json") == 0)
        json_mode = true;

    if (!json_mode)
        printf("RISC-V 32 Pipelined C Simulator!\n");

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file_path> [--json]\n", argv[0]);
        return EXIT_FAILURE;
    }

    memset(x, 0, sizeof(x));
    pc = 0;

    uint32_t count = load_file(argv[1], memory);
    if (count == 0xFFFFFFFF) return EXIT_FAILURE;

    PipelineStats stats = run_pipeline();

    x[0] = 0;
    bin_dump_registers(x, REG_COUNT, argv[1]);

    if (json_mode) {
        fprintf(stdout,
            "{\"instructions\":%u,\"cycles\":%u,\"stalls\":%u,\"flushes\":%u,\"cpi\":%.2f}\n",
            stats.instructions, stats.cycles, stats.stalls, stats.flushes, stats.cpi);
    }

    return 0;
}
