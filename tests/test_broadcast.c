/* Verifica end-to-end di asm/broadcast.s su griglia RxC.
   Uso: test_broadcast <file.o> <righe> <colonne> <valore atteso>

   Differenza rispetto a test_catena.c: li' il risultato stava in un punto
   solo (il sink), qui l'asserzione e' su TUTTE le RxC celle — "raggiunge
   tutti" e' proprio la proprieta' da dimostrare.

   Stampa anche la mappa delle attese (s4), che disegna l'onda diagonale: il
   valore deve crescere con r+c. Se una cella lontana ha aspettato meno di una
   vicina, la propagazione non e' quella che credi. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "core.h"
#include "grid.h"
#include "elf.h"

#define MAX_CICLI 1000000   /* tetto anti-deadlock: se lo tocca, il test fallisce */

/* indici dei registri (vedi REG_NAMES in core.c) */
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
    check_elf(elf);
    Elf32_Ehdr *h = (Elf32_Ehdr *)elf;

    Grid g;
    grid_init(&g, rows, cols, h -> e_entry);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            carica_elf_in_core(grid_at(&g, r, c), elf, h);

    freopen("/dev/null", "w", stdout);   /* la traccia per istruzione qui e' rumore */

    int cicli = 0, vivi;
    do {
        grid_step(&g);
        cicli++;
        vivi = 0;
        for (int i = 0; i < rows * cols; i++) vivi |= g.cores[i].running;
    } while (vivi && cicli < MAX_CICLI);

    /* il rischio vero di questo programma e' che una cella resti in attesa di
       un vicino che non le mandera' mai niente: si manifesta qui */
    assert(cicli < MAX_CICLI);
    assert(!vivi);

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            RISC_V *k = grid_at(&g, r, c);

            /* identita' cablata */
            assert((int)k -> regs[A0] == r);
            assert((int)k -> regs[A1] == c);
            assert((int)k -> regs[A2] == rows);
            assert((int)k -> regs[A3] == cols);

            /* il dato e' arrivato fin qui */
            assert((int)k -> regs[S1] == atteso);
        }
    }

    fprintf(stderr, "%-14s %dx%-3d ok  valore=%d  cicli=%d\n",
            argv[1], rows, cols, atteso, cicli);
    for (int r = 0; r < rows; r++) {
        fprintf(stderr, "%16s s4:", "");
        for (int c = 0; c < cols; c++)
            fprintf(stderr, " %5u", grid_at(&g, r, c) -> regs[S4]);
        fprintf(stderr, "\n");
    }

    grid_free(&g);
    free(elf);
    return 0;
}
