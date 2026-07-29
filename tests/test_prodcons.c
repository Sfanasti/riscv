/* Verifica end-to-end di asm/prodcons.s su griglia 1x2.
   Uso: test_prodcons <file.o> <somma attesa>
   Gira il programma fino a terminazione e controlla i registri direttamente,
   senza passare dalla stampa: niente parsing di stdout da tenere allineato. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "core.h"
#include "grid.h"
#include "elf.h"

#define MAX_CICLI 100000   /* tetto anti-deadlock: se lo tocca, il test fallisce */

/* indici dei registri usati da prodcons.s (vedi REG_NAMES in core.c) */
#define A0 10
#define S1  9
#define S2 18
#define S3 19
#define S4 20

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "uso: %s <file.o> <somma attesa>\n", argv[0]);
        return 1;
    }
    int attesa = atoi(argv[2]);

    long size;
    uint8_t *elf = load_elf(argv[1], &size);
    check_elf(elf);
    Elf32_Ehdr *h = (Elf32_Ehdr *)elf;

    Grid g;
    grid_init(&g, 1, 2, h -> e_entry);
    for (int c = 0; c < 2; c++) carica_elf_in_core(grid_at(&g, 0, c), elf, h);

    RISCV_Core *prod = grid_at(&g, 0, 0);
    RISCV_Core *cons = grid_at(&g, 0, 1);

    freopen("/dev/null", "w", stdout);   /* la traccia per istruzione qui e' rumore */

    int cicli = 0;
    while ((prod -> running || cons -> running) && cicli < MAX_CICLI) {
        grid_step(&g);
        cicli++;
    }

    /* terminazione: entrambi fermi per ECALL, non per tetto raggiunto */
    assert(cicli < MAX_CICLI);
    assert(!prod -> running && !cons -> running);

    /* identita': stesso ELF, due ruoli */
    assert(prod -> regs[A0] == 0 && cons -> regs[A0] == 1);

    /* il risultato, e la prova che ci e' arrivato per la strada giusta */
    assert((int)cons -> regs[S1] == attesa);
    assert(prod -> regs[S2] == 0);   /* il produttore ha spedito tutto */
    assert(cons -> regs[S2] == 0);   /* il consumatore ha letto tutto */

    /* i contatori non sono assertabili: a ritardo basso aspettano entrambi
       (il produttore ritenta, il consumatore rispina). Si stampano e basta. */
    fprintf(stderr, "%-24s ok  somma=%d  cicli=%d  ritentativi=%u  attese=%u\n",
            argv[1], attesa, cicli, prod -> regs[S3], cons -> regs[S4]);

    grid_free(&g);
    free(elf);
    return 0;
}
