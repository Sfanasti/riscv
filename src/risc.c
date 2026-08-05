#include "risc.h"
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

/*
    NOBP=1 -> modalità senza backpressure, si veda il case PCIO. Letta una volta
    sola e non più riletta: stessa convenzione di STEP e BORDO in main.c, ma qui
    la variabile serve anche ai test, che non passano da main.
*/
static int nobp(void) {
    static int v = -1;
    if (v < 0) {
        v = getenv("NOBP") != NULL;
    }
    return v;
}

void print_state(RISC_V *risc) {
    printf("\n--- STATO RISC [%d] | PC: 0x%08x | INSTR: 0x%08x ---\n",
           risc -> risc_id, risc -> pc, risc -> current_inst);

    for (int i = 0; i < 32; i++) {
        printf("x%02d (%-4s) = %-10d ", i, REG_NAMES[i], (int32_t)risc -> regs[i]);
        if ((i + 1) % 4 == 0) {
            printf("\n");
        }
    }

    printf("IN:  ");

    for (int d = 0; d < 4; d++){
        Channel *c = risc -> in_ch[d];
        printf("%s=%-6d%c ", DIR_NAMES[d], c ? c -> data : 0,
               c && ch_isrdy(c) ? '*' : '.');
    }

    printf("\nOUT: ");

    for (int d = 0; d < 4; d++){
        printf("%s=%-6d%c ", DIR_NAMES[d], risc -> out_ch[d].data,
               ch_isrdy(&risc -> out_ch[d]) ? '*' : '.');
        }
    printf("\n");
}

void init_risc(RISC_V *risc, uint32_t start_pc, int id) {
    for (int i = 0; i < 32; i++){
        risc -> regs[i] = 0;
    }

    for (int i = 0; i < 4096; i++){
        risc -> memory[i] = 0;
    }

    for (int i = 0; i < 4; i++) {
        risc -> out_ch[i] = (Channel){0};
        risc -> in_ch[i] = NULL;
    }

    risc -> pc = start_pc;
    risc -> running = true;
    risc -> current_inst = 0;
    risc -> risc_id = id;
    risc -> attese = 0;
    risc -> ritentativi = 0;
    /*
        i registri di identità (a0..a3 = riga, colonna, righe, colonne) li
        scrive grid_init: qui non si sa niente della topologia
    */
}

uint32_t fetch(RISC_V *risc) {
    if (risc -> pc / 4 >= 4096) {
        printf("Errore: PC fuori dai limiti della memoria!\n");
        risc -> running = 0;
        return 0;
    }

    uint32_t instr = risc -> memory[risc -> pc / 4];

    risc -> current_inst = instr;

    risc -> pc += 4;

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
        if (imm_i & 0x800) {
            imm_i = (int32_t)(imm_i << 20) >> 20;
        }
        d.imm = imm_i;
    }

    if (d.opcode == STORE) {
        int32_t imm_s = ((instr >> 7) & 0x1F) | (((instr >> 25) & 0x7F) << 5);

        if (imm_s & 0x800) {
            imm_s |= 0xFFFFF000;
        }
        d.imm = imm_s;
    }

    if (d.opcode == BRANCH) {
        int32_t imm_b = ((instr >> 7) & 0x1E)
                      | ((instr >> 20) & 0x7E0)
                      | ((instr << 4) & 0x800)
                      | ((instr >> 19) & 0x1000);
        if (imm_b & 0x1000) {
            imm_b |= 0xFFFFE000;
        }
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
        if (imm_j & 0x100000) {
            imm_j |= 0xFFE00000;
        }
        d.imm = imm_j;
    }
    return d;
}

/*
    Larghezza in byte per funct3 di LOAD/STORE; 0 = codifica non implementata.
    LB LH LW - LBU LHU - -
*/
static const int LS_WIDTH[8] = { 1, 2, 4, 0, 1, 2, 0, 0 };

/*
    La RAM del RISC è byte-indirizzata (come la vede il caricatore ELF, che fa
    memcpy su (uint8_t*)memory + sh_addr): "memory" è uint32_t[] solo per
    comodità del fetch.
    L'indirizzo NON è quindi un indice di parola.
    Senza questo controllo uno store oltre i 16 KB finiva nel RISC successivo
    dell'array flat di grid.c, corrompendolo.
*/
static uint8_t *mem_ptr(RISC_V *risc, uint32_t addr, int width) {
    if (width == 0 || addr > (uint32_t)sizeof(risc -> memory) - (uint32_t)width) {
        printf("[RISC %d] accesso a 0x%08x (%d byte) fuori RAM -> stop\n",
               risc -> risc_id, addr, width);
        risc -> running = false;
        return NULL;
    }
    return (uint8_t *)risc -> memory + addr;
}

void execute(RISC_V *risc, DecodedInstr d) {
    uint32_t address;
    uint32_t val1, val2;
    int branch_taken;

    switch (d.opcode) {

        case LOAD:
            {
                address = risc -> regs[d.rs1] + d.imm;
                uint8_t *p = mem_ptr(risc, address, LS_WIDTH[d.funct3]);
                if (!p) {
                    break;
                }

                int16_t h; int32_t w;
                switch (d.funct3) {
                    case 0x0:
                        risc -> regs[d.rd] = (uint32_t)(int32_t)(int8_t)*p;
                        printf("LB x%d, %d(x%d)\n", d.rd, d.imm, d.rs1); break;

                    case 0x1:
                        memcpy(&h, p, 2);
                        risc -> regs[d.rd] = (uint32_t)(int32_t)h;
                        printf("LH x%d, %d(x%d)\n", d.rd, d.imm, d.rs1); break;

                    case 0x2:
                        memcpy(&w, p, 4);
                        risc -> regs[d.rd] = (uint32_t)w;
                        printf("LW x%d, %d(x%d)\n", d.rd, d.imm, d.rs1); break;

                    case 0x4:
                        risc -> regs[d.rd] = *p;
                        printf("LBU x%d, %d(x%d)\n", d.rd, d.imm, d.rs1); break;

                    case 0x5:
                        memcpy(&h, p, 2);
                        risc -> regs[d.rd] = (uint16_t)h;
                        printf("LHU x%d, %d(x%d)\n", d.rd, d.imm, d.rs1); break;
                }
            }
            break;

        case STORE:
            {
                address = risc -> regs[d.rs1] + d.imm;
                uint8_t *p = mem_ptr(risc, address, LS_WIDTH[d.funct3]);
                if (!p) {
                    break;
                }

                uint32_t v = risc -> regs[d.rs2];
                memcpy(p, &v, (size_t)LS_WIDTH[d.funct3]);   /* little-endian: i byte bassi per primi */
                printf("%s x%d, %d(x%d)\n",
                       d.funct3 == 0x0 ? "SB" : d.funct3 == 0x1 ? "SH" : "SW",
                       d.rs2, d.imm, d.rs1);
            }
            break;

        case OP_IMM:
            switch (d.funct3) {
                case 0x0:
                    risc -> regs[d.rd] = risc -> regs[d.rs1] + d.imm;
                    printf("ADDI x%d, x%d, %d\n", d.rd, d.rs1, d.imm); break;

                case 0x2:
                    risc -> regs[d.rd] = ((int32_t)risc -> regs[d.rs1] < (int32_t)d.imm) ? 1 : 0;
                    printf("SLTI x%d, x%d, %d\n", d.rd, d.rs1, d.imm); break;

                case 0x3:
                    risc -> regs[d.rd] = (risc -> regs[d.rs1] < (uint32_t)d.imm) ? 1 : 0;
                    printf("SLTIU x%d, x%d, %d\n", d.rd, d.rs1, d.imm); break;

                case 0x4:
                    risc -> regs[d.rd] = risc -> regs[d.rs1] ^ d.imm;
                    printf("XORI x%d, x%d, %d\n", d.rd, d.rs1, d.imm); break;

                case 0x6:
                    risc -> regs[d.rd] = risc -> regs[d.rs1] | d.imm;
                    printf("ORI x%d, x%d, %d\n", d.rd, d.rs1, d.imm); break;

                case 0x7:
                    risc -> regs[d.rd] = risc -> regs[d.rs1] & d.imm;
                    printf("ANDI x%d, x%d, %d\n", d.rd, d.rs1, d.imm); break;

                case 0x1:
                    risc -> regs[d.rd] = risc -> regs[d.rs1] << (d.imm & 0x1F);
                    printf("SLLI x%d, x%d, %d\n", d.rd, d.rs1, d.imm & 0x1F); break;

                case 0x5:
                    if (d.funct7 == 0x20) {
                        risc -> regs[d.rd] = (int32_t)risc -> regs[d.rs1] >> (d.imm & 0x1F);
                        printf("SRAI x%d, x%d, %d\n", d.rd, d.rs1, d.imm & 0x1F);
                    } else {
                        risc -> regs[d.rd] = (uint32_t)risc -> regs[d.rs1] >> (d.imm & 0x1F);
                        printf("SRLI x%d, x%d, %d\n", d.rd, d.rs1, d.imm & 0x1F);
                    }
                    break;
            }
            break;

        case OP:
            switch (d.funct3) {
                case 0x0:
                    if (d.funct7 == 0x20) {
                        risc -> regs[d.rd] = risc -> regs[d.rs1] - risc -> regs[d.rs2];
                        printf("SUB x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else if (d.funct7 == 0x01) {
                        risc -> regs[d.rd] = (int32_t)risc -> regs[d.rs1] * (int32_t)risc -> regs[d.rs2];
                        printf("MUL x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        risc -> regs[d.rd] = risc -> regs[d.rs1] + risc -> regs[d.rs2];
                        printf("ADD x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;

                case 0x1:
                    if (d.funct7 == 0x01) {
                        int64_t full_res = (int64_t)(int32_t)risc -> regs[d.rs1] * (int64_t)(int32_t)risc -> regs[d.rs2];
                        risc -> regs[d.rd] = (uint32_t)(full_res >> 32);
                        printf("MULH x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        risc -> regs[d.rd] = risc -> regs[d.rs1] << (risc -> regs[d.rs2] & 0x1F);
                        printf("SLL x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;

                case 0x4:
                    if (d.funct7 == 0x01) {
                        if (risc -> regs[d.rs2] == 0) {
                            risc -> regs[d.rd] = 0xFFFFFFFF;
                        } else {
                            risc -> regs[d.rd] = (int32_t)risc -> regs[d.rs1] / (int32_t)risc -> regs[d.rs2];
                        }
                        printf("DIV x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        risc -> regs[d.rd] = risc -> regs[d.rs1] ^ risc -> regs[d.rs2];
                        printf("XOR x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;

                case 0x6:
                    if (d.funct7 == 0x01) {
                        if (risc -> regs[d.rs2] == 0) {
                            risc -> regs[d.rd] = risc -> regs[d.rs1];
                        } else {
                            risc -> regs[d.rd] = (int32_t)risc -> regs[d.rs1] % (int32_t)risc -> regs[d.rs2];
                        }
                        printf("REM x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        risc -> regs[d.rd] = risc -> regs[d.rs1] | risc -> regs[d.rs2];
                        printf("OR x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;

                case 0x2:
                    if (d.funct7 == 0x01) {
                        int64_t full_res = (int64_t)(int32_t)risc -> regs[d.rs1] * (int64_t)(uint64_t)risc -> regs[d.rs2];
                        risc -> regs[d.rd] = (uint32_t)(full_res >> 32);
                        printf("MULHSU x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        risc -> regs[d.rd] = ((int32_t)risc -> regs[d.rs1] < (int32_t)risc -> regs[d.rs2]) ? 1 : 0;
                        printf("SLT x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;

                case 0x3:
                    if (d.funct7 == 0x01) {
                        uint64_t full_res = (uint64_t)risc -> regs[d.rs1] * (uint64_t)risc -> regs[d.rs2];
                        risc -> regs[d.rd] = (uint32_t)(full_res >> 32);
                        printf("MULHU x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        risc -> regs[d.rd] = (risc -> regs[d.rs1] < risc -> regs[d.rs2]) ? 1 : 0;
                        printf("SLTU x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;

                case 0x5:
                    if (d.funct7 == 0x20) {
                        risc -> regs[d.rd] = (int32_t)risc -> regs[d.rs1] >> (risc -> regs[d.rs2] & 0x1F);
                        printf("SRA x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else if (d.funct7 == 0x01) {
                        if (risc -> regs[d.rs2] == 0) {
                            risc -> regs[d.rd] = 0xFFFFFFFF;
                        } else {
                            risc -> regs[d.rd] = risc -> regs[d.rs1] / risc -> regs[d.rs2];
                        }
                        printf("DIVU x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        risc -> regs[d.rd] = (uint32_t)risc -> regs[d.rs1] >> (risc -> regs[d.rs2] & 0x1F);
                        printf("SRL x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;

                case 0x7:
                    if (d.funct7 == 0x01) {
                        if (risc -> regs[d.rs2] == 0) {
                            risc -> regs[d.rd] = risc -> regs[d.rs1];
                        } else {
                            risc -> regs[d.rd] = risc -> regs[d.rs1] % risc -> regs[d.rs2];
                        }
                        printf("REMU x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    } else {
                        risc -> regs[d.rd] = risc -> regs[d.rs1] & risc -> regs[d.rs2];
                        printf("AND x%d, x%d, x%d\n", d.rd, d.rs1, d.rs2);
                    }
                    break;
            }
            break;

        case JALR:
            {
                uint32_t return_addr = risc -> pc;
                risc -> pc = (risc -> regs[d.rs1] + d.imm) & ~1;
                risc -> regs[d.rd] = return_addr;
                printf("JALR x%d, x%d, %d\n", d.rd, d.rs1, d.imm);
            }
            break;

        case BRANCH:
            branch_taken = 0;
            val1 = risc -> regs[d.rs1];
            val2 = risc -> regs[d.rs2];
            switch (d.funct3) {
                case 0x0: if (val1 == val2) { branch_taken = 1; }
                    printf("BEQ ");
                    break;

                case 0x1: if (val1 != val2) { branch_taken = 1; }
                    printf("BNE ");
                    break;

                case 0x4: if ((int32_t)val1 < (int32_t)val2) { branch_taken = 1; }
                    printf("BLT ");
                    break;

                case 0x5: if ((int32_t)val1 >= (int32_t)val2) { branch_taken = 1; }
                    printf("BGE ");
                    break;

                case 0x6: if (val1 < val2) { branch_taken = 1; }
                    printf("BLTU ");
                    break;

                case 0x7: if (val1 >= val2) { branch_taken = 1; }
                    printf("BGEU ");
                    break;

            }
            if (branch_taken) {
                risc -> pc = (risc -> pc - 4) + d.imm;
            }
            printf("x%d, x%d, %d\n", d.rs1, d.rs2, d.imm);
            break;

        case LUI:
            risc -> regs[d.rd] = d.imm;
            printf("LUI x%d, 0x%x\n", d.rd, d.imm);
            break;

        case AUIPC:
            risc -> regs[d.rd] = (risc -> pc - 4) + d.imm;
            printf("AUIPC x%d, 0x%x\n", d.rd, d.imm);
            break;


        case JAL:
            risc -> regs[d.rd] = risc -> pc;
            risc -> pc = (risc -> pc - 4) + d.imm;
            printf("JAL x%d, %d\n", d.rd, d.imm);
            break;

        case PCIO:
            {
                int dir = d.rs2;

                /*
                    in_ch è NULL solo su un risc fuori griglia (modalità single-risc):
                    un ingresso scollegato è un canale eternamente vuoto,
                    ISRDY dà 0 e IN dà 0. In griglia sono tutti cablati, bordo
                    compreso, quindi qui non cambia niente.
                */
                Channel *in = risc -> in_ch[dir];

                /*
                    Modalità senza backpressure (NOBP=1): il canale degrada a un
                    registro senza handshake, cioè il systolic in lockstep puro
                    che il ready bit sostituisce.
                    OUT sovrascrive anche uno slot non ancora letto, SETRDY non
                    fallisce mai, ISRDY dice sempre di sì.
                    Il doppio buffer data/data_next resta, quindi fra le
                    due modalità cambia SOLO il controllo di flusso: la latenza
                    di un ciclo per hop e l'ordine di visibilità sono identici.
                    Serve a far perdere dati, non a funzionare: in pratica utilizzato
                    solo per avere un confronto con gli altri dati.
                */
                if (nobp()) {
                    if (d.funct3 == 0x0){
                        risc -> regs[d.rd] = in ? in -> data : 0;          /* IN */
                    }
                    else if (d.funct3 == 0x1){                             /* OUT */
                        /*
                            Qui SETRDY non commuta wp, quindi la cattura di
                            ch_commit (che scatta sulla transizione) non
                            scatterebbe mai e il dato non arriverebbe. La OUT
                            forza la transizione da sé: è esattamente il canale
                            senza handshake, dove pubblicare non richiede il
                            permesso di nessuno.
                        */
                        Channel *o = &risc -> out_ch[dir];
                        o -> data_next = risc -> regs[d.rs1];
                        o -> wp_next   = o -> wp ^ 1;
                    }
                    else {
                        risc -> regs[d.rd] = 1;   /* ISRDY e SETRDY: sempre */
                    }
                    break;
                }

                if (d.funct3 == 0x0) { /* IN */
                    risc  ->  regs[d.rd] = in ? ch_read_c(in) : 0;
                    printf("IN x%d, DIR:%d (valore: %d)\n", d.rd, dir, risc -> regs[d.rd]);
                }
                else if (d.funct3 == 0x1) { /* OUT */
                    ch_write(&risc->out_ch[dir], risc->regs[d.rs1]);
                    printf("OUT x%d, DIR:%d (valore: %d)\n", d.rs1, dir, risc -> regs[d.rs1]);
                }
                else if(d.funct3 == 0x2) { /* ISRDY */
                    risc  ->  regs[d.rd] = in ? ch_isrdy(in) : 0;
                    if (!risc -> regs[d.rd]) {
                        risc -> attese++;
                    }
                    printf("ISRDY x%d, DIR:%d (esito: %d)\n", d.rd, dir, risc->regs[d.rd]);
                }
                else if(d.funct3 == 0x3) { /* SETRDY */
                    risc  ->  regs[d.rd] = ch_setrdy(&risc  ->  out_ch[dir]);
                    if (!risc -> regs[d.rd]) {
                        risc -> ritentativi++;
                    }
                    printf("SETRDY x%d, DIR:%d (esito: %d)\n", d.rd, dir, risc->regs[d.rd]);
                }
                break;
            }

        case ECALL:
            risc -> running = false;
            printf("ECALL -> RISC %d fermato\n", risc->risc_id);
            break;

        default:
            risc -> running = false;
            printf("[RISC %d] pc=0x%08x instr=0x%08x opcode 0x%02x non implementato -> stop\n",
                   risc -> risc_id, risc -> pc - 4, risc -> current_inst, d.opcode);
            break;
    }

    risc -> regs[0] = 0;
}

void execute_step(RISC_V *risc) {
    if (!risc -> running) {
        return;
    }

    risc -> current_inst = fetch(risc);

    DecodedInstr d = decode(risc -> current_inst);

    execute(risc, d);
}
