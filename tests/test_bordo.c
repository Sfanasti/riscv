/*
    Verifica dell'I/O di bordo dell'host su asm/bordo.s, griglia RxC.
    Uso: test_bordo <file.o> <righe> <colonne> <valore>

    L'host fa il vicino che le celle di perimetro non hanno, spinge a
    NORD sulla prima riga e drena SUD sull'ultima.

    Le due asserzioni che contano:
      - il valore arriva in fondo a ogni colonna --> la spinta funziona
      - ne esce esattamente uno per colonna --> il drenaggio funziona e
        non duplica: se grid_pop non consumasse, l'ultima riga si
        bloccherebbe sulla SETRDY e il test morirebbe sul tetto dei cicli.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "core.h"
#include "grid.h"
#include "elf.h"

#define MAX_CICLI 100000   /* tetto anti-deadlock: se lo tocca, il test fallisce */
#define S1 9               /* il valore ricevuto (si veda REG_NAMES in core.c) */

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "uso: %s <file.o> <righe> <colonne> <valore>\n", argv[0]);
        return 1;
    }
    int rows   = atoi(argv[2]);
    int cols   = atoi(argv[3]);
    uint32_t valore = (uint32_t)strtoul(argv[4], NULL, 0);
    assert(rows >= 1 && cols >= 1);

    long size;
    uint8_t *elf = load_elf(argv[1], &size);
    check_elf(elf);
    Elf32_Ehdr *h = (Elf32_Ehdr *)elf;

    Grid g;
    grid_init(&g, rows, cols, h -> e_entry);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            carica_elf_in_core(grid_at(&g, r, c), elf, h);
        }
    }

    /* la traccia per istruzione qui è rumore: il risultato esce su stderr */
    freopen("/dev/null", "w", stdout);

    /*
        una sola push per colonna: a griglia appena nata i canali di bordo
        sono vuoti, quindi deve essere accettata subito
    */

    for (int c = 0; c < cols; c++) {
        assert(grid_push(&g, 0, c, NORD, valore) == 1);
    }

    /* e una seconda nello stesso ciclo no perché lo slot è già impegnato */
    for (int c = 0; c < cols; c++) {
        assert(grid_push(&g, 0, c, NORD, valore + 1) == 0);
    }

    int usciti = 0, cicli = 0, vivi;
    do {
        /*
            l'host drena il perimetro sud PRIMA del passo, come fosse un vicino 
            vero: la lettura entra nel "next" e il consumo diventa visibile al commit
        */
        for (int c = 0; c < cols; c++) {
            uint32_t v;
            if (grid_pop(&g, rows - 1, c, SUD, &v)) {
                assert(v == valore);     /* il dato è passato intatto per 'rows' hop */
                usciti++;
            }
        }

        grid_step(&g);
        cicli++;
        vivi = 0;
        for (int i = 0; i < rows * cols; i++) {
            vivi |= g.cores[i].running;
        }
    } while (vivi && cicli < MAX_CICLI);

    /*
        i core si fermano sulla ecall, ma l'ultimo valore esce dal bordo sud nel
        ciclo del commit successivo: serve dunque un giro in più per raccoglierlo
    */
    for (int c = 0; c < cols; c++) {
        uint32_t v;
        if (grid_pop(&g, rows - 1, c, SUD, &v)) { assert(v == valore); usciti++; }
    }

    assert(cicli < MAX_CICLI);
    assert(!vivi);
    assert(usciti == cols);              /* uno per colonna, né perso né duplicato */

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            assert(grid_at(&g, r, c) -> regs[S1] == valore);
        }
    }

    unsigned ritentativi, attese;
    grid_spin(&g, &ritentativi, &attese);
    fprintf(stderr, "%d,%d,%d,%u,%u,%u,%d\n",
            rows, cols, cicli, ritentativi, attese, valore, usciti);

    grid_free(&g);
    free(elf);
    return 0;
}
