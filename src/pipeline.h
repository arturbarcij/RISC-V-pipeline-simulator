/**
 * 5-Stage Pipelined RISC-V RV32I Simulator
 * Pipeline register definitions, control signals, and prototypes.
 */

#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Bit extraction helpers ---- */
static inline uint32_t get_bits(uint32_t x, int hi, int lo) {
    int w = hi - lo + 1;
    return (x >> lo) & ((w == 32) ? 0xFFFFFFFFu : ((1u << w) - 1u));
}
static inline uint32_t get_rd(uint32_t i)     { return get_bits(i, 11, 7);  }
static inline uint32_t get_funct3(uint32_t i) { return get_bits(i, 14, 12); }
static inline uint32_t get_rs1(uint32_t i)    { return get_bits(i, 19, 15); }
static inline uint32_t get_rs2(uint32_t i)    { return get_bits(i, 24, 20); }
static inline uint32_t get_funct7(uint32_t i) { return get_bits(i, 31, 25); }

/* ---- Sign extension helpers ---- */
static inline int32_t sign_ext(uint32_t val, int bits) {
    int shift = 32 - bits;
    return ((int32_t)(val << shift)) >> shift;
}

/* ---- Pipeline register structs ---- */

typedef struct {
    uint32_t instruction;
    uint32_t pc;
    bool     valid;
} IF_ID_Reg;

typedef struct {
    uint32_t pc;
    uint32_t rs1_val;
    uint32_t rs2_val;
    int32_t  imm;
    uint32_t rd;
    uint32_t rs1;
    uint32_t rs2;
    uint32_t opcode;
    uint32_t funct3;
    uint32_t funct7;
    bool     reg_write;
    bool     mem_read;
    bool     mem_write;
    bool     branch;
    bool     is_jal;
    bool     is_jalr;
    bool     valid;
} ID_EX_Reg;

typedef struct {
    uint32_t alu_result;
    uint32_t rs2_val;       /* store data (forwarded) */
    uint32_t rd;
    uint32_t funct3;
    bool     reg_write;
    bool     mem_read;
    bool     mem_write;
    bool     valid;
} EX_MEM_Reg;

typedef struct {
    uint32_t rd;
    uint32_t write_data;
    bool     reg_write;
    bool     valid;
} MEM_WB_Reg;

/* ---- External state (defined in main.c) ---- */
extern uint32_t pc;
extern uint32_t x[32];

/* ---- Pipeline entry point ---- */
void run_pipeline(void);

#endif /* PIPELINE_H */
