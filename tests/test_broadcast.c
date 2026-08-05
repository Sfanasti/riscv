/*
    Verifica end-to-end di asm/broadcast.s su griglia RxC.
    Uso: test_broadcast <file.o> <righe> <colonne> <valore atteso>

    Differenza rispetto a test_catena.c: lì il risultato stava in un punto
    solo (il sink), qui l'asserzione è su TUTTE le RxC celle: va dimostrato
    che raggiunge tutti.

    Stampa anche la mappa delle attese (s4), che disegna l'onda diagonale: il
    valore deve crescere con r+c. Se una cella lontana ha aspettato meno di una
    vicina, la propagazione è sbagliata.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "risc.h"
#include "grid.h"
#include "elf.h"

#define MAX_CICLI 1000000   /* tetto anti-deadlock: se lo tocca, il test fallisce */

/* indici dei registri (si veda REG_NAMES in risc.c) */
#define A0 10   /* riga */
#define A1 11   /* colonna */
#define A2 12   /* righe totali */
#define A3 13   /* colonne totali */
#define S1  9   /* il valore ricevuto */
#define S4 20   /* cicli passati ad aspettare */

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "uso: %s <file.o> <righe> <colonne> <valore atteso>\n", argv[0]);
        return 1;
    }
    int rows   = atoi(argv[2]);
    int cols   = atoi(argv[3]);
    int atteso = atoi(argv[4]);
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
    freopen("/dev/null", "w", stdout);

    int cicli = 0, vivi;
    do {
        grid_step(&g);
        cicli++;
        vivi = 0;
        for (int i = 0; i < rows * cols; i++) {
            vivi |= g.risc[i].running;
        }
    } while (vivi && cicli < MAX_CICLI);

    /*
        il rischio vero di questo programma è che una cella resti in attesa di
        un vicino che non le manderà mai niente: si manifesta qui
    */
    assert(cicli < MAX_CICLI);
    assert(!vivi);

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            RISC_V *k = grid_at(&g, r, c);

            /* identità cablata */
            assert((int)k -> regs[A0] == r);
            assert((int)k -> regs[A1] == c);
            assert((int)k -> regs[A2] == rows);
            assert((int)k -> regs[A3] == cols);

            /* il dato è arrivato fin qui */
            assert((int)k -> regs[S1] == atteso);
        }
    }

    unsigned ritentativi, attese;
    grid_spin(&g, &ritentativi, &attese);
    fprintf(stderr, "%d,%d,%d,%u,%u,%d\n", rows, cols, cicli, ritentativi, attese, atteso);

    /*
        La mappa cella per cella dell'attesa: cresce con r+c, è la forma
        dell'onda che attraversa la griglia. Una riga per riga della griglia,
        quindi va tolta di mezzo quando l'uscita deve restare un CSV.
    */
    if (!getenv("CSV")) {
        for (int r = 0; r < rows; r++) {
            fprintf(stderr, "%16s s4:", "");
            for (int c = 0; c < cols; c++) {
                fprintf(stderr, " %5u", grid_at(&g, r, c) -> regs[S4]);
            }
            fprintf(stderr, "\n");
        }
    }

    grid_free(&g);
    free(elf);
    return 0;
}
