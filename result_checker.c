#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define TEST_COUNT 29

// test names
const char *tests[TEST_COUNT] = {
    "addlarge", "addneg", "addpos", "bool",
    "branchcnt", "branchmany", "branchtrap",
    "loop", "recursive", "set",
    "shift", "shift2", "string", "width",
    "t1", "t2", "t3", "t4", "t5",
    "t6", "t7", "t8", "t9", "t10",
    "t11", "t12", "t13", "t14", "t15"
};

bool compare_files(const char *file1, const char *file2) {
    FILE *f1 = fopen(file1, "rb");
    FILE *f2 = fopen(file2, "rb");

    if (!f1 || !f2) {
        printf("  ERROR: Could not open %s or %s\n", file1, file2);
        if (f1) fclose(f1);
        if (f2) fclose(f2);
        return false;
    }

    uint32_t w1, w2;
    size_t r1, r2;
    long word_index = 0;

    while (1) {
        r1 = fread(&w1, sizeof(uint32_t), 1, f1);
        r2 = fread(&w2, sizeof(uint32_t), 1, f2);

        if (r1 == 0 || r2 == 0) {
            if (r1 == r2) { 
                fclose(f1);
                fclose(f2);
                return true;        // both ended normally
            } else {
                printf("  Files differ in length\n");
                fclose(f1);
                fclose(f2);
                return false;
            }
        }

        if (w1 != w2) {
            printf("  MISMATCH at word %ld: 0x%08X != 0x%08X\n",
                   word_index, w1, w2);
            fclose(f1);
            fclose(f2);
            return false;
        }

        word_index++;
    }
}

int main(void) {
    printf("\n=== RISC-V Simulator Result Checker ===\n\n");

    for (int i = 0; i < TEST_COUNT; i++) {
        char bin_path[256];
        char res_path[256];

        // Your dumps: results/register_dump_<name>.bin
        snprintf(bin_path, sizeof(bin_path),
                 "results/register_dump_%s.bin", tests[i]);

        // Reference results: test_result_files/<name>.res
        snprintf(res_path, sizeof(res_path),
                 "test_result_files/%s.res", tests[i]);

        printf("Checking %-11s ... ", tests[i]);

        if (compare_files(bin_path, res_path)) {
            printf("PASS\n");
        } else {
            printf("FAIL\n");
        }
    }

    return 0;
}
