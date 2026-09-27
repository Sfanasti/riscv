#ifndef RISC_H
#define RISC_H

#include <stdint.h>
#include <stdbool.h>
#include "channel.h"

/* in parole: 16 KB */
#ifndef MEM_SIZE
#define MEM_SIZE 4096
#endif

typedef struct {
    int risc_id;
    uint32_t regs[32];
    uint32_t pc;
    uint32_t current_inst;
    uint32_t memory[MEM_SIZE];
    bool running;

    Channel out_ch[4];
    Channel *in_ch[4];

    uint32_t attese;        /* ISRDY a vuoto */
    uint32_t ritentativi;   /* SETRDY rifiutate */
} RISC_V;

void print_state(RISC_V *risc);
void init_risc(RISC_V *risc, uint32_t start_pc, int id);

typedef struct {
    uint32_t opcode;
    uint32_t rd;
    uint32_t rs1;
    uint32_t rs2;
    uint32_t funct3;
    uint32_t funct7;
    int32_t imm;
} DecodedInstr;

#define NORD  0
#define EST   1
#define SUD   2
#define OVEST 3

#define OP_IMM 0x13
#define LOAD   0x03
#define JALR   0x67
#define STORE  0x23
#define BRANCH 0x63
#define LUI    0x37
#define AUIPC  0x17
#define JAL    0x6F
#define OP     0x33
#define ECALL  0x73
#define PCIO   0x0B     /* custom-0: IN, OUT, ISRDY, SETRDY */

uint32_t fetch(RISC_V *risc);
DecodedInstr decode(uint32_t instr);
void execute(RISC_V *risc, DecodedInstr d);
void execute_step(RISC_V *risc);

#endif
