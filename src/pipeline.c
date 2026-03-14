/**
 * 5-Stage Pipelined RISC-V RV32I Simulator
 *
 * Stages: IF -> ID -> EX -> MEM -> WB
 * Features: data forwarding, load-use stall, branch/jump flush
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pipeline.h"
#include "memory.h"

/* ================================================================
 *  Pipeline state
 * ================================================================ */

static IF_ID_Reg  if_id,  if_id_next;
static ID_EX_Reg  id_ex,  id_ex_next;
static EX_MEM_Reg ex_mem, ex_mem_next;
static MEM_WB_Reg mem_wb, mem_wb_next;

static bool ecall_in_pipeline;   /* ECALL detected, draining */
static bool flush_signal;        /* EX wants to flush IF/ID & ID/EX */
static uint32_t flush_target;    /* new PC after flush */

static uint32_t cycle_count;
static uint32_t instr_count;     /* retired instructions */

#define MAX_CYCLES 500000

/* ================================================================
 *  Immediate decode (all formats)
 * ================================================================ */

static int32_t decode_immediate(uint32_t instr, uint32_t opcode) {
    switch (opcode) {
    /* I-type */
    case 0x13: case 0x03: case 0x67:
        return sign_ext((instr >> 20) & 0xFFF, 12);

    /* S-type */
    case 0x23: {
        uint32_t imm = (get_bits(instr, 31, 25) << 5) | get_bits(instr, 11, 7);
        return sign_ext(imm, 12);
    }

    /* SB-type */
    case 0x63: {
        uint32_t imm = (get_bits(instr, 31, 31) << 12)
                      | (get_bits(instr, 7, 7) << 11)
                      | (get_bits(instr, 30, 25) << 5)
                      | (get_bits(instr, 11, 8) << 1);
        return sign_ext(imm, 13);
    }

    /* U-type  (already shifted) */
    case 0x37: case 0x17:
        return (int32_t)(instr & 0xFFFFF000u);

    /* UJ-type */
    case 0x6F: {
        uint32_t imm = (get_bits(instr, 31, 31) << 20)
                      | (get_bits(instr, 19, 12) << 12)
                      | (get_bits(instr, 20, 20) << 11)
                      | (get_bits(instr, 30, 21) << 1);
        return sign_ext(imm, 21);
    }

    default:
        return 0;
    }
}

/* ================================================================
 *  ALU
 * ================================================================ */

static uint32_t alu_compute(uint32_t a, uint32_t b,
                            uint32_t funct3, uint32_t funct7,
                            uint32_t opcode)
{
    /* R-type: both operands from registers */
    if (opcode == 0x33) {
        switch (funct3) {
        case 0x0: return (funct7 == 0x20)
                         ? (uint32_t)((int32_t)a - (int32_t)b)   /* SUB */
                         : (uint32_t)((int32_t)a + (int32_t)b);  /* ADD */
        case 0x1: return a << (b & 0x1F);                        /* SLL */
        case 0x2: return ((int32_t)a < (int32_t)b) ? 1u : 0u;   /* SLT */
        case 0x3: return (a < b) ? 1u : 0u;                     /* SLTU */
        case 0x4: return a ^ b;                                  /* XOR */
        case 0x5: return (funct7 == 0x20)
                         ? (uint32_t)(((int32_t)a) >> (b & 0x1F))  /* SRA */
                         : a >> (b & 0x1F);                        /* SRL */
        case 0x6: return a | b;                                  /* OR  */
        case 0x7: return a & b;                                  /* AND */
        }
    }

    /* I-type ALU (opcode 0x13) — b is the immediate */
    if (opcode == 0x13) {
        uint32_t imm = (uint32_t)b;   /* b already holds the sign-extended imm */
        uint32_t shamt = imm & 0x1F;
        switch (funct3) {
        case 0x0: return (uint32_t)((int32_t)a + (int32_t)imm);            /* ADDI  */
        case 0x1: return a << shamt;                                       /* SLLI  */
        case 0x2: return ((int32_t)a < (int32_t)imm) ? 1u : 0u;           /* SLTI  */
        case 0x3: return (a < imm) ? 1u : 0u;                             /* SLTIU */
        case 0x4: return a ^ imm;                                         /* XORI  */
        case 0x5: {
            if (funct7 == 0x20)
                return (uint32_t)(((int32_t)a) >> shamt);                  /* SRAI  */
            else
                return a >> shamt;                                         /* SRLI  */
        }
        case 0x6: return a | imm;                                         /* ORI   */
        case 0x7: return a & imm;                                         /* ANDI  */
        }
    }

    /* Load / Store: address = base + offset */
    if (opcode == 0x03 || opcode == 0x23)
        return (uint32_t)((int32_t)a + (int32_t)b);

    /* LUI */
    if (opcode == 0x37)
        return (uint32_t)b;   /* imm already holds upper 20 bits shifted */

    /* AUIPC */
    if (opcode == 0x17)
        return 0;  /* handled specially in EX stage */

    return 0;
}

/* ================================================================
 *  Branch evaluation
 * ================================================================ */

static bool evaluate_branch(uint32_t a, uint32_t b, uint32_t funct3) {
    switch (funct3) {
    case 0x0: return a == b;                              /* BEQ  */
    case 0x1: return a != b;                              /* BNE  */
    case 0x4: return (int32_t)a <  (int32_t)b;            /* BLT  */
    case 0x5: return (int32_t)a >= (int32_t)b;            /* BGE  */
    case 0x6: return a <  b;                              /* BLTU */
    case 0x7: return a >= b;                              /* BGEU */
    default:  return false;
    }
}

/* ================================================================
 *  Load from memory (consolidates LB/LH/LW/LBU/LHU)
 * ================================================================ */

static uint32_t load_memory(uint32_t addr, uint32_t funct3) {
    switch (funct3) {
    case 0x0: { /* LB — sign-extend byte */
        uint32_t b = read_byte(addr);
        return (b & 0x80) ? (b | 0xFFFFFF00u) : b;
    }
    case 0x1: { /* LH — sign-extend halfword */
        uint32_t lo = read_byte(addr);
        uint32_t hi = read_byte(addr + 1);
        uint32_t half = (hi << 8) | lo;
        return (half & 0x8000) ? (half | 0xFFFF0000u) : half;
    }
    case 0x2: { /* LW */
        uint32_t b0 = read_byte(addr);
        uint32_t b1 = read_byte(addr + 1);
        uint32_t b2 = read_byte(addr + 2);
        uint32_t b3 = read_byte(addr + 3);
        return (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
    }
    case 0x4: { /* LBU — zero-extend byte */
        return read_byte(addr);
    }
    case 0x5: { /* LHU — zero-extend halfword */
        uint32_t lo = read_byte(addr);
        uint32_t hi = read_byte(addr + 1);
        return (hi << 8) | lo;
    }
    default:
        return 0;
    }
}

/* ================================================================
 *  Store to memory (SB/SH/SW)
 * ================================================================ */

static void store_memory(uint32_t addr, uint32_t val, uint32_t funct3) {
    switch (funct3) {
    case 0x0: /* SB */
        write_byte(addr, val & 0xFF);
        break;
    case 0x1: /* SH */
        write_byte(addr,     val & 0xFF);
        write_byte(addr + 1, (val >> 8) & 0xFF);
        break;
    case 0x2: /* SW */
        write_byte(addr,     (val >>  0) & 0xFF);
        write_byte(addr + 1, (val >>  8) & 0xFF);
        write_byte(addr + 2, (val >> 16) & 0xFF);
        write_byte(addr + 3, (val >> 24) & 0xFF);
        break;
    }
}

/* ================================================================
 *  Forwarding unit
 * ================================================================ */

static uint32_t forward_rs1(void) {
    uint32_t rs = id_ex.rs1;
    if (rs == 0) return 0;

    /* EX/MEM has priority (more recent) */
    if (ex_mem.valid && ex_mem.reg_write && ex_mem.rd != 0 && ex_mem.rd == rs)
        return ex_mem.alu_result;

    /* MEM/WB */
    if (mem_wb.valid && mem_wb.reg_write && mem_wb.rd != 0 && mem_wb.rd == rs)
        return mem_wb.write_data;

    return id_ex.rs1_val;
}

static uint32_t forward_rs2(void) {
    uint32_t rs = id_ex.rs2;
    if (rs == 0) return 0;

    if (ex_mem.valid && ex_mem.reg_write && ex_mem.rd != 0 && ex_mem.rd == rs)
        return ex_mem.alu_result;

    if (mem_wb.valid && mem_wb.reg_write && mem_wb.rd != 0 && mem_wb.rd == rs)
        return mem_wb.write_data;

    return id_ex.rs2_val;
}

/* ================================================================
 *  Hazard detection
 * ================================================================ */

/* Does the instruction in IF/ID use rs1? */
static bool instr_uses_rs1(uint32_t opcode) {
    /* U-type and UJ-type do not use rs1 */
    return opcode != 0x37 && opcode != 0x17 && opcode != 0x6F;
}

/* Does the instruction in IF/ID use rs2? */
static bool instr_uses_rs2(uint32_t opcode) {
    /* R-type, S-type, SB-type use rs2 */
    return opcode == 0x33 || opcode == 0x23 || opcode == 0x63;
}

static bool detect_load_use_hazard(void) {
    if (!id_ex.valid || !id_ex.mem_read || id_ex.rd == 0)
        return false;
    if (!if_id.valid)
        return false;

    uint32_t instr = if_id.instruction;
    uint32_t opcode = instr & 0x7F;
    uint32_t rs1 = get_rs1(instr);
    uint32_t rs2 = get_rs2(instr);

    if (instr_uses_rs1(opcode) && rs1 == id_ex.rd)
        return true;
    if (instr_uses_rs2(opcode) && rs2 == id_ex.rd)
        return true;

    return false;
}

/* ================================================================
 *  Stage WB
 * ================================================================ */

static void stage_WB(void) {
    if (!mem_wb.valid || !mem_wb.reg_write || mem_wb.rd == 0)
        return;
    x[mem_wb.rd] = mem_wb.write_data;
    instr_count++;
}

/* ================================================================
 *  Stage MEM
 * ================================================================ */

static void stage_MEM(void) {
    if (!ex_mem.valid) {
        mem_wb_next.valid = false;
        return;
    }

    uint32_t data = ex_mem.alu_result;

    if (ex_mem.mem_read)
        data = load_memory(ex_mem.alu_result, ex_mem.funct3);

    if (ex_mem.mem_write)
        store_memory(ex_mem.alu_result, ex_mem.rs2_val, ex_mem.funct3);

    mem_wb_next.rd         = ex_mem.rd;
    mem_wb_next.write_data = data;
    mem_wb_next.reg_write  = ex_mem.reg_write;
    mem_wb_next.valid      = true;

    /* Count non-writing instructions (stores, branches) as retired here */
    if (!ex_mem.reg_write)
        instr_count++;
}

/* ================================================================
 *  Stage EX
 * ================================================================ */

static void stage_EX(void) {
    if (!id_ex.valid) {
        ex_mem_next.valid = false;
        return;
    }

    uint32_t op1 = forward_rs1();
    uint32_t op2 = forward_rs2();
    uint32_t alu_result = 0;

    uint32_t opcode = id_ex.opcode;
    uint32_t funct3 = id_ex.funct3;
    uint32_t funct7 = id_ex.funct7;
    int32_t  imm    = id_ex.imm;

    switch (opcode) {
    case 0x33: /* R-type */
        alu_result = alu_compute(op1, op2, funct3, funct7, opcode);
        break;

    case 0x13: /* I-type ALU */
        alu_result = alu_compute(op1, (uint32_t)imm, funct3, funct7, opcode);
        break;

    case 0x03: /* Load: addr = rs1 + imm */
        alu_result = (uint32_t)((int32_t)op1 + imm);
        break;

    case 0x23: /* Store: addr = rs1 + imm */
        alu_result = (uint32_t)((int32_t)op1 + imm);
        break;

    case 0x63: /* Branch */ {
        bool taken = evaluate_branch(op1, op2, funct3);
        if (taken) {
            flush_signal = true;
            flush_target = (uint32_t)((int32_t)id_ex.pc + imm);
        }
        /* Branches don't write registers */
        break;
    }

    case 0x37: /* LUI */
        alu_result = (uint32_t)imm;
        break;

    case 0x17: /* AUIPC */
        alu_result = id_ex.pc + (uint32_t)imm;
        break;

    case 0x6F: /* JAL: rd = pc+4, jump to pc+imm */
        alu_result = id_ex.pc + 4;
        flush_signal = true;
        flush_target = (uint32_t)((int32_t)id_ex.pc + imm);
        break;

    case 0x67: /* JALR: rd = pc+4, jump to (rs1+imm)&~1 */
        alu_result = id_ex.pc + 4;
        flush_signal = true;
        flush_target = ((uint32_t)((int32_t)op1 + imm)) & ~1u;
        break;
    }

    ex_mem_next.alu_result = alu_result;
    ex_mem_next.rs2_val    = op2;
    ex_mem_next.rd         = id_ex.rd;
    ex_mem_next.funct3     = id_ex.funct3;
    ex_mem_next.reg_write  = id_ex.reg_write;
    ex_mem_next.mem_read   = id_ex.mem_read;
    ex_mem_next.mem_write  = id_ex.mem_write;
    ex_mem_next.valid      = true;
}

/* ================================================================
 *  Stage ID
 * ================================================================ */

static void stage_ID(bool stall, bool flush) {
    if (stall || flush || !if_id.valid) {
        id_ex_next.valid = false;
        return;
    }

    uint32_t instr  = if_id.instruction;
    uint32_t opcode = instr & 0x7F;

    /* ECALL */
    if (instr == 0x73) {
        ecall_in_pipeline = true;
        id_ex_next.valid = false;
        return;
    }

    id_ex_next.pc      = if_id.pc;
    id_ex_next.opcode  = opcode;
    id_ex_next.rd      = get_rd(instr);
    id_ex_next.rs1     = get_rs1(instr);
    id_ex_next.rs2     = get_rs2(instr);
    id_ex_next.funct3  = get_funct3(instr);
    id_ex_next.funct7  = get_funct7(instr);
    id_ex_next.imm     = decode_immediate(instr, opcode);

    /* Read register file */
    id_ex_next.rs1_val = x[id_ex_next.rs1];
    id_ex_next.rs2_val = x[id_ex_next.rs2];

    /* Control signals */
    id_ex_next.reg_write = (opcode == 0x33 || opcode == 0x13 ||
                            opcode == 0x03 || opcode == 0x37 ||
                            opcode == 0x17 || opcode == 0x6F ||
                            opcode == 0x67);
    id_ex_next.mem_read  = (opcode == 0x03);
    id_ex_next.mem_write = (opcode == 0x23);
    id_ex_next.branch    = (opcode == 0x63);
    id_ex_next.is_jal    = (opcode == 0x6F);
    id_ex_next.is_jalr   = (opcode == 0x67);

    id_ex_next.valid     = true;
}

/* ================================================================
 *  Stage IF
 * ================================================================ */

static void stage_IF(bool stall, bool flush) {
    if (stall) {
        /* Keep if_id_next = if_id (unchanged) — handled by not overwriting */
        return;
    }

    if (flush) {
        if_id_next.valid = false;
        return;
    }

    if (pc >= MAX_MEMORY * 4) {
        if_id_next.valid = false;
        return;
    }

    if_id_next.instruction = get_memory(pc);
    if_id_next.pc          = pc;
    if_id_next.valid       = true;
    pc += 4;
}

/* ================================================================
 *  Main pipeline loop
 * ================================================================ */

void run_pipeline(void) {
    /* Clear all pipeline registers */
    memset(&if_id,  0, sizeof(if_id));
    memset(&id_ex,  0, sizeof(id_ex));
    memset(&ex_mem, 0, sizeof(ex_mem));
    memset(&mem_wb, 0, sizeof(mem_wb));

    ecall_in_pipeline = false;
    flush_signal      = false;
    cycle_count       = 0;
    instr_count       = 0;

    bool draining = false;

    while (1) {
        cycle_count++;

        /* ---- Prepare next-cycle copies ---- */
        if_id_next  = if_id;   /* default: hold (for stall case) */
        id_ex_next.valid  = false;
        ex_mem_next.valid = false;
        mem_wb_next.valid = false;

        /* ---- Detect load-use hazard (before running stages) ---- */
        bool stall = false;
        if (!draining)
            stall = detect_load_use_hazard();

        /* ---- Reset flush (EX may set it this cycle) ---- */
        flush_signal = false;

        /* ---- Run stages in reverse order ---- */
        stage_WB();
        stage_MEM();
        stage_EX();

        /* ---- Capture flush from EX ---- */
        bool flush = flush_signal;

        if (!draining) {
            stage_ID(stall, flush);
            stage_IF(stall, flush);
        } else {
            /* Draining: no new fetches */
            if_id_next.valid = false;
            id_ex_next.valid = false;
        }

        /* ---- Redirect PC on flush ---- */
        if (flush && !draining) {
            pc = flush_target;
        }

        /* ---- Advance pipeline registers ---- */
        if (!stall) {
            if_id = if_id_next;
        }
        /* On stall, if_id keeps its old value (already default) */

        id_ex  = id_ex_next;
        ex_mem = ex_mem_next;
        mem_wb = mem_wb_next;

        /* ---- ECALL drain logic ---- */
        if (ecall_in_pipeline && !draining) {
            draining = true;
        }

        if (draining) {
            if (!id_ex.valid && !ex_mem.valid && !mem_wb.valid)
                break;
        }

        /* Safety limit */
        if (cycle_count >= MAX_CYCLES) {
            printf("Cycle limit (%u) reached. Halting.\n", MAX_CYCLES);
            break;
        }
    }

    printf("\n=== Pipeline Statistics ===\n");
    printf("Cycles: %u\n", cycle_count);
    printf("Instructions retired: %u\n", instr_count);
    if (instr_count > 0)
        printf("CPI: %.2f\n", (double)cycle_count / instr_count);
}
