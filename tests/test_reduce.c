/* Verifica end-to-end di asm/reduce.s su griglia RxC.
   Uso: test_reduce <file.o> <righe> <colonne>

   Il totale atteso non e' un argomento: lo ricava il test dalla forma della
   griglia, come fa il Makefile con la somma di prodcons. Un valore scritto a
   mano si adatterebbe al bug invece di scoprirlo.

   Tre asserzioni, dalla piu' locale alla piu' globale, cosi' un fallimento
   dice DOVE si e' rotta la riduzione e non solo che il totale non torna:
     - ogni cella non-ultima-colonna ha il prefisso della sua riga
     - ogni cella dell'ultima colonna ha le righe 0..r sommate per intero
     - dal bordo sud-est esce un solo valore, ed e' il totale */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "core.h"
#include "grid.h"
#include "elf.h"

#define MAX_CICLI 1000000   /* tetto anti-deadlock: se lo tocca, il test fallisce */
#define S1 9                /* l'accumulatore (vedi REG_NAMES in core.c) */

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
    check_elf(elf);
    Elf32_Ehdr *h = (Elf32_Ehdr *)elf;

    Grid g;
    grid_init(&g, rows, cols, h -> e_entry);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            carica_elf_in_core(grid_at(&g, r, c), elf, h);

    freopen("/dev/null", "w", stdout);   /* la traccia per istruzione qui e' rumore */

    /* il risultato esce dal SUD dell'angolo sud-est: senza qualcuno che lo
       consuma quella cella resterebbe bloccata sulla propria SETRDY */
    long totale = 0;
    int  usciti = 0, cicli = 0, vivi;
    do {
        uint32_t v;
        if (grid_pop(&g, rows - 1, cols - 1, SUD, &v)) { totale = (int32_t)v; usciti++; }

        grid_step(&g);
        cicli++;
        vivi = 0;
        for (int i = 0; i < rows * cols; i++) vivi |= g.cores[i].running;
    } while (vivi && cicli < MAX_CICLI);

    /* l'ultima pubblicazione viene committata nel giro in cui il core si ferma:
       un pop in piu' per raccoglierla */
    {
        uint32_t v;
        if (grid_pop(&g, rows - 1, cols - 1, SUD, &v)) { totale = (int32_t)v; usciti++; }
    }

    /* qui muore un deadlock: una cella in attesa di un parziale che non arriva */
    assert(cicli < MAX_CICLI);
    assert(!vivi);

    /* parziali di riga: la cella (r,c) ha sommato le colonne 0..c della sua riga */
    for (int r = 0; r < rows; r++) {
        long riga = 0;
        for (int c = 0; c < cols - 1; c++) {
            riga += r + c;
            assert((long)(int32_t)grid_at(&g, r, c) -> regs[S1] == riga);
        }
    }

    /* parziali di colonna: la cella (r,C-1) ha sommato le righe 0..r per intero */
    long atteso = 0;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) atteso += r + c;
        assert((long)(int32_t)grid_at(&g, r, cols - 1) -> regs[S1] == atteso);
    }

    assert(usciti == 1);        /* un solo risultato: ne' perso ne' duplicato */
    assert(totale == atteso);

    fprintf(stderr, "%-14s %dx%-3d ok  totale=%ld  cicli=%d\n",
            argv[1], rows, cols, totale, cicli);

    grid_free(&g);
    free(elf);
    return 0;
}
