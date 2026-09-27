/*
    Verifica di reduce.s su griglia RxC: parziali di riga e di colonna, e un
    solo valore uscito dal bordo sud-est, pari alla somma di r+c.
    Uso: test_reduce <file.o> <righe> <colonne>
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "risc.h"
#include "grid.h"
#include "elf.h"

#define MAX_CICLI 1000000   /* tetto: se lo tocca, il test fallisce */
#define S1 9                /* l'accumulatore */

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "uso: %s <file.o> <righe> <colonne>\n", argv[0]);
        return 1;
    }
    int rows = atoi(argv[2]);
    int cols = atoi(argv[3]);
    assert(rows >= 1 && cols >= 1);

    long size;
    uint8_t *elf = load_elf(argv[1], &size);
    check_elf(elf, size);
    Elf32_Ehdr *h = (Elf32_Ehdr *)elf;

    Grid g;
    grid_init(&g, rows, cols, h -> e_entry);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            carica_elf_in_risc(grid_at(&g, r, c), elf, h, size);
        }
    }

    /* la traccia per istruzione qui è rumore: il risultato esce su stderr */
    if (freopen("/dev/null", "w", stdout) == NULL) {
        fprintf(stderr, "Errore: impossibile silenziare stdout\n");
        return 1;
    }

    /* il totale esce dal SUD dell'angolo sud-est e va drenato */
    long totale = 0;
    int  usciti = 0, cicli = 0, vivi;
    do {
        uint32_t v;
        if (grid_pop(&g, rows - 1, cols - 1, SUD, &v)) { totale = (int32_t)v; usciti++; }

        grid_step(&g);
        cicli++;
        vivi = 0;
        for (int i = 0; i < rows * cols; i++) {
            vivi |= g.risc[i].running;
        }
    } while (vivi && cicli < MAX_CICLI);

    /* l'ultimo valore è visibile al commit del ciclo dell'ECALL */
    {
        uint32_t v;
        if (grid_pop(&g, rows - 1, cols - 1, SUD, &v)) { totale = (int32_t)v; usciti++; }
    }

    /* terminazione per ECALL, non per tetto */
    assert(cicli < MAX_CICLI);
    assert(!vivi);

    /* la cella (r,c) ha sommato le colonne 0..c della sua riga */
    for (int r = 0; r < rows; r++) {
        long riga = 0;
        for (int c = 0; c < cols - 1; c++) {
            riga += r + c;
            assert((long)(int32_t)grid_at(&g, r, c) -> regs[S1] == riga);
        }
    }

    /* la cella (r,C-1) ha sommato le righe 0..r per intero */
    long atteso = 0;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            atteso += r + c;
        }
        assert((long)(int32_t)grid_at(&g, r, cols - 1) -> regs[S1] == atteso);
    }

    assert(usciti == 1);        /* né perso né duplicato */
    assert(totale == atteso);

    unsigned ritentativi, attese;
    grid_spin(&g, &ritentativi, &attese);
    fprintf(stderr, "%d,%d,%d,%u,%u,%ld\n",
            rows, cols, cicli, ritentativi, attese, totale);

    grid_free(&g);
    free(elf);
    return 0;
}
