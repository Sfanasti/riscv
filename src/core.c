#include "core.h"
#include "channel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const char *REG_NAMES[32] = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

static const char *DIR_NAMES[4] = { "NORD", "EST", "SUD", "OVEST" };

void print_state(RISC_V *core) {
    printf("\n--- STATO CORE [%d] | PC: 0x%08x | INSTR: 0x%08x ---\n",
           core -> core_id, core -> pc, core -> current_inst);

    for (int i = 0; i < 32; i++) {
        printf("x%02d (%-4s) = %-10d ", i, REG_NAMES[i], (int32_t)core -> regs[i]);
        if ((i + 1) % 4 == 0) printf("\n");
    }

    printf("IN:  ");

    for (int d = 0; d < 4; d++){
        uint32_t v= core -> in_ch[d] ? core -> in_ch[d] -> data : 0;
        printf("%s=%-6d ", DIR_NAMES[d], v);
    }

    printf("\nOUT: ");

    for (int d = 0; d < 4; d++) printf("%s=%-6d ", DIR_NAMES[d], core -> out_ch[d].data);
    printf("\n");
}

void init_core(RISC_V *core, uint32_t start_pc, int id) {
    for (int i = 0; i < 32; i++) core -> regs[i] = 0;

    for (int i = 0; i < 4096; i++) core -> memory[i] = 0;

    for (int i = 0; i < 4; i++) {
        core -> out_ch[i] = (Channel){0};
        core -> in_ch[i] = NULL;
    }

    core -> pc = start_pc;
    core -> running = true;
    core -> current_inst = 0;
    core -> core_id = id;
    /* i registri di identita' (a0..a3 = riga, colonna, righe, colonne) li
       scrive grid_init: qui non si sa niente della topologia */
}

uint32_t fetch(RISC_V *core) {
    if (core -> pc / 4 >= 4096) {
        printf("Errore: PC fuori dai limiti della memoria!\n");
        core -> running = 0;
        return 0;
    }

    uint32_t instr = core -> memory[core -> pc / 4];

    core -> current_inst = instr;

    core -> pc += 4;

    return instr;
}

DecodedInstr decode(uint32_t instr) {
    DecodedInstr d;

    d.opcode = instr & 0x7F;
    d.rd     = (instr >> 7) & 0x1F;
    d.funct3 = (instr >> 12) & 0x7;
    d.rs1    = (instr >> 15) & 0x1F;
    d.rs2    = (instr >> 20) & 0x1F;
    d.funct7 = (instr >> 25) & 0x7F;
    d.imm    = 0;

    if (d.opcode == OP_IMM || d.opcode == LOAD || d.opcode == JALR) {
        int32_t imm_i = (instr >> 20) & 0xFFF;
        if (imm_i & 0x800)
            imm_i = (int32_t)(imm_i << 20) >> 20;
        d.imm = imm_i;
    }

    if (d.opcode == STORE) {
        int32_t imm_s = ((instr >> 7) & 0x1F) | (((instr >> 25) & 0x7F) << 5);

        if (imm_s & 0x800)
            imm_s |= 0xFFFFF000;
        d.imm = imm_s;
    }

    if (d.opcode == BRANCH) {
        int32_t imm_b = ((instr >> 7) & 0x1E)
                      | ((instr >> 20) & 0x7E0)
                      | ((instr << 4) & 0x800)
                      | ((instr >> 19) & 0x1000);
        if (imm_b & 0x1000)
            imm_b |= 0xFFFFE000;
        d.imm = imm_b;
    }

    if (d.opcode == LUI || d.opcode == AUIPC) {
        d.imm = instr & 0xFFFFF000;
    }

    if (d.opcode == JAL) {
        int32_t imm_j = ((instr >> 12) & 0xFF) << 12
                      | ((instr >> 20) & 0x1) << 11
                      | ((instr >> 21) & 0x3FF) << 1
                      | ((instr >> 31) & 0x1) << 20;
        if (imm_j & 0x100000)
            imm_j |= 0xFFE00000;
        d.imm = imm_j;
    }
    return d;
}

/* Larghezza in byte per funct3 di LOAD/STORE; 0 = codifica non implementata.
   LB LH LW - LBU LHU - -                                                     */
static const int LS_WIDTH[8] = { 1, 2, 4, 0, 1, 2, 0, 0 };

/* La RAM del core e' byte-indirizzata (come la vede il caricatore ELF, che fa
   memcpy su (uint8_t*)memory + sh_addr): 'memory' e' uint32_t[] solo per
   comodita' del fetch. L'indirizzo NON e' quindi un indice di parola.
   Senza questo controllo uno store oltre i 16 KB finiva nel core successivo
   dell'array flat di grid.c, corrompendolo in silenzio. */
static uint8_t *mem_ptr(RISC_V *core, uint32_t addr, int width) {
    if (width == 0 || addr > (uint32_t)sizeof(core -> memory) - (uint32_t)width) {
        printf("[core %d] accesso a 0x%08x (%d byte) fuori RAM -> stop\n",
               core -> core_id, addr, width);
        core -> running = false;
        return NULL;
    }
    return (uint8_t *)core -> memory + addr;
}

void execute(RISC_V *core, DecodedInstr d) {
    uint32_t address;
    uint32_t val1, val2;
    int branch_taken;

    switch (d.opcode) {

        case LOAD:
            {
                address = core -> regs[d.rs1] + d.imm;
                uint8_t *p = mem_ptr(core, address, LS_WIDTH[d.funct3]);
                if (!p) break;

                /* memcpy e non un cast a int32_t*: l'indirizzo puo' essere
                   disallineato e il cast sarebbe UB */
                int16_t h; int32_t w;
                switch (d.funct3) {
                    case 0x0:
                        core -> regs[d.rd] = (uint32_t)(int32_t)(int8_t)*p;
                        printf("LB x%d, %d(x%d)\n", d.rd, d.imm, d.rs1); break;

                    case 0x1:
                        memcpy(&h, p, 2);
                        core -> regs[d.rd] = (uint32_t)(int32_t)h;
                        printf("LH x%d, %d(x%d)\n", d.rd, d.imm, d.rs1); break;

                    case 0x2:
                        memcpy(&w, p, 4);
                        core -> regs[d.rd] = (uint32_t)w;
                        printf("LW x%d, %d(x%d)\n", d.rd, d.imm, d.rs1); break;

                    case 0x4:
                        core -> regs[d.rd] = *p;
                        printf("LBU x%d, %d(x%d)\n", d.rd, d.imm, d.rs1); break;

                    case 0x5:
                        memcpy(&h, p, 2);
                        core -> regs[d.rd] = (uint16_t)h;
                        printf("LHU x%d, %d(x%d)\n", d.rd, d.imm, d.rs1); break;
                }
            }
            break;

        case STORE:
            {
                address = core -> regs[d.rs1] + d.imm;
                uint8_t *p = mem_ptr(core, address, LS_WIDTH[d.funct3]);
                if (!p) break;

                uint32_t v = core -> regs[d.rs2];
                memcpy(p, &v, (size_t)LS_WIDTH[d.funct3]);   /* little-endian: i byte bassi per primi */
                printf("%s x%d, %d(x%d)\n",
                       d.funct3 == 0x0 ? "SB" : d.funct3 == 0x1 ? "SH" : "SW",
                       d.rs2, d.imm, d.rs1);
            }
            break;

        case OP_IMM:
            switch (d.funct3) {
                case 0x0:
                    core -> regs[d.rd] = core -> regs[d.rs1] + d.imm;
                    printf("ADDI x%d, x%d, %d\n", d.rd, d.rs1, d.imm); break;

                case 0x2:
                    core -> regs[d.rd] = ((int32_t)core -> regs[d.rs1] < (int32_t)d.imm) ? 1 : 0;
                    printf("SLTI x%d, x%d, %d\n", d.rd, d.rs1, d.imm); break;

                case 0x3:
                    core -> regs[d.rd] = (core -> regs[d.rs1] < (uint32_t)d.imm) ? 1 : 0;
                    printf("SLTIU x%d, x%d, %d\n", d.rd, d.rs1, d.imm); break;

                case 0x4:
                    core -> regs[d.rd] = core -> regs[d.rs1] ^ d.imm;
                    printf("XORI x%d, x%d, %d\n", d.rd, d.rs1, d.imm); break;

                case 0x6:
                    core -> regs[d.rd] = core -> regs[d.rs1] | d.imm;
                    printf("ORI x%d, x%d, %d\n", d.rd, d.rs1, d.imm); break;

                case 0x7:
                    core -> regs[d.rd] = core -> regs[d.rs1] & d.imm;
                    printf("ANDI x%d, x%d, %d\n", d.rd, d.rs1, d.imm); break;

                case 0x1:
                    core -> regs[d.rd] = core -> regs[d.rs1] << (d.imm & 0x1F);
                    printf("SLLI x%d, x%d, %d\n", d.rd, d.rs1, d.imm & 0x1F); break;

                case 0x5:
                    if (d.funct7 == 0x20) {
                        core -> regs[d.rd] = (int32_t)core -> regs[d.rs1] >> (d.imm & 0x1F);
                        printf("SRAI x%d, x%d, %d\n", d.rd, d.rs1, d.imm & 0x1F);
                    } else {
                        core -> regs[d.rd] = (uint32_t)core -> regs[d.rs1] >> (d.imm & 0x1F);
                        printf("SRLI x%d, x%d, %d\n", d.rd, d.rs1, d.imm & 0x1F);
                    }
                    break;
            }
            break;

        case OP:
            switch (d.funct3) {
                case 0x0:
                    if (d.funct7 == 0x20) {
                        core -> regs[d.rd] = core -> regs[d.rs1] - core -> regs[d.rs2];
                        printf("SUB x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else if (d.funct7 == 0x01) {
                        core -> regs[d.rd] = (int32_t)core -> regs[d.rs1] * (int32_t)core -> regs[d.rs2];
                        printf("MUL x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        core -> regs[d.rd] = core -> regs[d.rs1] + core -> regs[d.rs2];
                        printf("ADD x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;

                case 0x1:
                    if (d.funct7 == 0x01) {
                        int64_t full_res = (int64_t)(int32_t)core -> regs[d.rs1] * (int64_t)(int32_t)core -> regs[d.rs2];
                        core -> regs[d.rd] = (uint32_t)(full_res >> 32);
                        printf("MULH x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        core -> regs[d.rd] = core -> regs[d.rs1] << (core -> regs[d.rs2] & 0x1F);
                        printf("SLL x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;

                case 0x4:
                    if (d.funct7 == 0x01) {
                        if (core -> regs[d.rs2] == 0) core -> regs[d.rd] = 0xFFFFFFFF;
                        else core -> regs[d.rd] = (int32_t)core -> regs[d.rs1] / (int32_t)core -> regs[d.rs2];
                        printf("DIV x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        core -> regs[d.rd] = core -> regs[d.rs1] ^ core -> regs[d.rs2];
                        printf("XOR x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;

                case 0x6:
                    if (d.funct7 == 0x01) {
                        if (core -> regs[d.rs2] == 0) core -> regs[d.rd] = core -> regs[d.rs1];
                        else core -> regs[d.rd] = (int32_t)core -> regs[d.rs1] % (int32_t)core -> regs[d.rs2];
                        printf("REM x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        core -> regs[d.rd] = core -> regs[d.rs1] | core -> regs[d.rs2];
                        printf("OR x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;

                case 0x2:
                    if (d.funct7 == 0x01) {
                        int64_t full_res = (int64_t)(int32_t)core -> regs[d.rs1] * (int64_t)(uint64_t)core -> regs[d.rs2];
                        core -> regs[d.rd] = (uint32_t)(full_res >> 32);
                        printf("MULHSU x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        core -> regs[d.rd] = ((int32_t)core -> regs[d.rs1] < (int32_t)core -> regs[d.rs2]) ? 1 : 0;
                        printf("SLT x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;

                case 0x3:
                    if (d.funct7 == 0x01) {
                        uint64_t full_res = (uint64_t)core -> regs[d.rs1] * (uint64_t)core -> regs[d.rs2];
                        core -> regs[d.rd] = (uint32_t)(full_res >> 32);
                        printf("MULHU x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        core -> regs[d.rd] = (core -> regs[d.rs1] < core -> regs[d.rs2]) ? 1 : 0;
                        printf("SLTU x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;

                case 0x5:
                    if (d.funct7 == 0x20) {
                        core -> regs[d.rd] = (int32_t)core -> regs[d.rs1] >> (core -> regs[d.rs2] & 0x1F);
                        printf("SRA x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else if (d.funct7 == 0x01) {
                        if (core -> regs[d.rs2] == 0) core -> regs[d.rd] = 0xFFFFFFFF;
                        else core -> regs[d.rd] = core -> regs[d.rs1] / core -> regs[d.rs2];
                        printf("DIVU x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        core -> regs[d.rd] = (uint32_t)core -> regs[d.rs1] >> (core -> regs[d.rs2] & 0x1F);
                        printf("SRL x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;

                case 0x7:
                    if (d.funct7 == 0x01) {
                        if (core -> regs[d.rs2] == 0) core -> regs[d.rd] = core -> regs[d.rs1];
                        else core -> regs[d.rd] = core -> regs[d.rs1] % core -> regs[d.rs2];
                        printf("REMU x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        core -> regs[d.rd] = core -> regs[d.rs1] & core -> regs[d.rs2];
                        printf("AND x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;
            }
            break;

        case JALR:
            {
                uint32_t return_addr = core -> pc;
                core -> pc = (core -> regs[d.rs1] + d.imm) & ~1;
                core -> regs[d.rd] = return_addr;
                printf("JALR x%d, x%d, %d\n", d.rd, d.rs1, d.imm);
            }
            break;

        case BRANCH:
            branch_taken = 0;
            val1 = core -> regs[d.rs1];
            val2 = core -> regs[d.rs2];
            switch (d.funct3) {
                case 0x0: if (val1 == val2) branch_taken = 1;
                    printf("BEQ "); break;
                case 0x1: if (val1 != val2) branch_taken = 1;
                    printf("BNE "); break;
                case 0x4: if ((int32_t)val1 < (int32_t)val2) branch_taken = 1;
                    printf("BLT "); break;
                case 0x5: if ((int32_t)val1 >= (int32_t)val2) branch_taken = 1;
                    printf("BGE "); break;
                case 0x6: if (val1 < val2) branch_taken = 1;
                    printf("BLTU "); break;
                case 0x7: if (val1 >= val2) branch_taken = 1;
                    printf("BGEU "); break;
            }
            if (branch_taken) core -> pc = (core -> pc - 4) + d.imm;
            printf("x%d, x%d, %d\n", d.rs1, d.rs2, d.imm);
            break;

        case LUI:
            core -> regs[d.rd] = d.imm;
            printf("LUI x%d, 0x%x\n", d.rd, d.imm); break;
        case AUIPC:
            core -> regs[d.rd] = (core -> pc - 4) + d.imm;
            printf("AUIPC x%d, 0x%x\n", d.rd, d.imm); break;

        case JAL:
            core -> regs[d.rd] = core -> pc;
            core -> pc = (core -> pc - 4) + d.imm;
            printf("JAL x%d, %d\n", d.rd, d.imm);
            break;

        case PCIO:
            {
                int dir = d.rs2;

                /* in_ch e' NULL solo su un core fuori griglia (modalita' singolo
                   core): un ingresso scollegato e' un canale eternamente vuoto,
                   ISRDY da' 0 e IN da' 0. In griglia sono tutti cablati, bordo
                   compreso, quindi qui non cambia niente. */
                Channel *in = core -> in_ch[dir];

                if (d.funct3 == 0x0) { //IN
                    core  ->  regs[d.rd] = in ? ch_read_c(in) : 0;
                    printf("IN x%d, DIR:%d (valore: %d)\n", d.rd, dir, core -> regs[d.rd]);
                }
                else if (d.funct3 == 0x1) { //OUT
                    ch_write(&core->out_ch[dir], core->regs[d.rs1]);
                    printf("OUT x%d, DIR:%d (valore: %d)\n", d.rs1, dir, core -> regs[d.rs1]);
                }
                else if(d.funct3 == 0x2) { //ISRDY
                    core  ->  regs[d.rd] = in ? ch_isrdy(in) : 0;
                    printf("ISRDY x%d, DIR:%d (esito: %d)\n", d.rd, dir, core->regs[d.rd]);
                }
                else if(d.funct3 == 0x3) { //SETRDY
                    core  ->  regs[d.rd] = ch_setrdy(&core  ->  out_ch[dir]);
                    printf("SETRDY x%d, DIR:%d (esito: %d)\n", d.rd, dir, core->regs[d.rd]);
                }
                break;
            }

        case ECALL:
            core -> running = false;
            printf("ECALL -> core %d fermato\n", core->core_id);
            break;

        default:
            core -> running = false;
            printf("[core %d] pc=0x%08x instr=0x%08x opcode 0x%02x non implementato -> stop\n",
                   core->core_id, core->pc - 4, core->current_inst, d.opcode);
            break;        
    }

    core -> regs[0] = 0;
}

void execute_step(RISC_V *core) {
    if (!core -> running) return;

    core -> current_inst = fetch(core);

    DecodedInstr d = decode(core -> current_inst);

    execute(core, d);
}
