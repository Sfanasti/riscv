/* Verifica end-to-end dei programmi a catena (prodcons.s, chain.s) su 1xC.
   Uso: test_catena <file.o> <colonne> <somma attesa>

   Legge i registri direttamente invece di fare parsing della stampa: niente
   formato di output da tenere allineato. Lo stesso binario serve entrambi i
   programmi, perche' prodcons.s e' il caso C=2 della stessa catena. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "core.h"
#include "grid.h"
#include "elf.h"

#define MAX_CICLI 1000000   /* tetto anti-deadlock: se lo tocca, il test fallisce */

/* indici dei registri usati dai programmi (vedi REG_NAMES in core.c) */
#define A0 10   /* riga */
#define A1 11   /* colonna */
#define A2 12   /* righe totali */
#define A3 13   /* colonne totali */
#define S1  9   /* somma, sul pozzo */
#define S2 18   /* quanti ne restano da trattare */
#define S3 19   /* SETRDY rifiutate */
#define S4 20   /* ISRDY a vuoto */

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "uso: %s <file.o> <colonne> <somma attesa>\n", argv[0]);
        return 1;
    }
    int cols   = atoi(argv[2]);
    int attesa = atoi(argv[3]);
    assert(cols >= 2);

    long size;
    uint8_t *elf = load_elf(argv[1], &size);
    check_elf(elf);
    Elf32_Ehdr *h = (Elf32_Ehdr *)elf;

    Grid g;
    grid_init(&g, 1, cols, h -> e_entry);
    for (int c = 0; c < cols; c++) carica_elf_in_core(grid_at(&g, 0, c), elf, h);

    freopen("/dev/null", "w", stdout);   /* la traccia per istruzione qui e' rumore */

    int cicli = 0, vivi;
    do {
        grid_step(&g);
        cicli++;
        vivi = 0;
        for (int c = 0; c < cols; c++) vivi |= grid_at(&g, 0, c) -> running;
    } while (vivi && cicli < MAX_CICLI);

    /* terminazione: tutti fermi per ECALL, non per tetto raggiunto */
    assert(cicli < MAX_CICLI);
    assert(!vivi);

    unsigned ritentativi = 0, attese = 0;
    for (int c = 0; c < cols; c++) {
        RISC_V *k = grid_at(&g, 0, c);

        /* identita' cablata: ogni cella sa dove si trova */
        assert(k -> regs[A0] == 0);
        assert((int)k -> regs[A1] == c);
        assert(k -> regs[A2] == 1);
        assert((int)k -> regs[A3] == cols);

        /* nessuno si e' fermato a meta' del proprio compito */
        assert(k -> regs[S2] == 0);

        ritentativi += k -> regs[S3];
        attese      += k -> regs[S4];
    }

    /* il risultato, sull'ultima colonna */
    assert((int)grid_at(&g, 0, cols - 1) -> regs[S1] == attesa);

    fprintf(stderr, "%-16s C=%-3d ok  somma=%d  cicli=%d  ritentativi=%u  attese=%u\n",
            argv[1], cols, attesa, cicli, ritentativi, attese);

    grid_free(&g);
    free(elf);
    return 0;
}
