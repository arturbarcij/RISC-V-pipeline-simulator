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
    printf("RISC-V 32 Pipelined C Simulator!\n");

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file_path>\n", argv[0]);
        fprintf(stderr, "Execute a binary file with RISC-V instructions!\n");
        return EXIT_FAILURE;
    }

    memset(x, 0, sizeof(x));
    pc = 0;

    uint32_t count = load_file(argv[1], memory);
    if (count == 0xFFFFFFFF) {
        printf("Error from load_file() received\n");
        return EXIT_FAILURE;
    }

    run_pipeline();

    /* Ensure x[0] is 0 */
    x[0] = 0;

    bin_dump_registers(x, REG_COUNT, argv[1]);

    return 0;
}
