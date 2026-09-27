/*
    Verifica dell'I/O di bordo su bordo.s, griglia RxC: l'host spinge a NORD
    sulla prima riga e drena SUD sull'ultima. Il valore deve arrivare in ogni
    cella e uscire una volta per colonna.
    Uso: test_bordo <file.o> <righe> <colonne> <valore>
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "risc.h"
#include "grid.h"
#include "elf.h"

#define MAX_CICLI 100000   /* tetto: se lo tocca, il test fallisce */
#define S1 9               /* il valore ricevuto */

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

    /* canali di bordo vuoti: la prima push è accettata... */
    for (int c = 0; c < cols; c++) {
        assert(grid_push(&g, 0, c, NORD, valore) == 1);
    }

    /* ...la seconda nello stesso ciclo no */
    for (int c = 0; c < cols; c++) {
        assert(grid_push(&g, 0, c, NORD, valore + 1) == 0);
    }

    int usciti = 0, cicli = 0, vivi;
    do {
        /* drenaggio prima del passo, come farebbe un vicino */
        for (int c = 0; c < cols; c++) {
            uint32_t v;
            if (grid_pop(&g, rows - 1, c, SUD, &v)) {
                assert(v == valore);     /* intatto dopo rows hop */
                usciti++;
            }
        }

        grid_step(&g);
        cicli++;
        vivi = 0;
        for (int i = 0; i < rows * cols; i++) {
            vivi |= g.risc[i].running;
        }
    } while (vivi && cicli < MAX_CICLI);

    /* l'ultimo valore è visibile al commit del ciclo dell'ECALL */
    for (int c = 0; c < cols; c++) {
        uint32_t v;
        if (grid_pop(&g, rows - 1, c, SUD, &v)) { assert(v == valore); usciti++; }
    }

    assert(cicli < MAX_CICLI);
    assert(!vivi);
    assert(usciti == cols);              /* né perso né duplicato */

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
