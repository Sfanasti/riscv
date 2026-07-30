/* Verifica dell'I/O di bordo dell'host su asm/bordo.s, griglia RxC.
   Uso: test_bordo <file.o> <righe> <colonne> <valore>

   E' l'unica clausola della specifica (sezione 5, "registri di bordo") che
   non aveva copertura: qui l'host fa il vicino che le celle di perimetro non
   hanno, spinge a NORD sulla prima riga e drena SUD sull'ultima.

   Le due asserzioni che contano:
     - il valore arriva in fondo a ogni colonna     -> la spinta funziona
     - ne esce esattamente uno per colonna          -> il drenaggio funziona e
       non duplica; se grid_pop non consumasse davvero, l'ultima riga si
       bloccherebbe sulla SETRDY e il test morirebbe sul tetto dei cicli. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "core.h"
#include "grid.h"
#include "elf.h"

#define MAX_CICLI 100000   /* tetto anti-deadlock: se lo tocca, il test fallisce */
#define S1 9               /* il valore ricevuto (vedi REG_NAMES in core.c) */

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
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            carica_elf_in_core(grid_at(&g, r, c), elf, h);

    freopen("/dev/null", "w", stdout);   /* la traccia per istruzione qui e' rumore */

    /* una sola spinta per colonna: a griglia appena nata i canali di bordo
       sono vuoti, quindi deve essere accettata subito */
    for (int c = 0; c < cols; c++)
        assert(grid_push(&g, 0, c, NORD, valore) == 1);

    /* e una seconda nello stesso ciclo no: lo slot e' gia' impegnato */
    for (int c = 0; c < cols; c++)
        assert(grid_push(&g, 0, c, NORD, valore + 1) == 0);

    int usciti = 0, cicli = 0, vivi;
    do {
        /* l'host drena il perimetro sud PRIMA del passo, come un vicino vero:
           la lettura entra nel "next" e il consumo diventa visibile al commit */
        for (int c = 0; c < cols; c++) {
            uint32_t v;
            if (grid_pop(&g, rows - 1, c, SUD, &v)) {
                assert(v == valore);     /* il dato e' passato intatto per rows hop */
                usciti++;
            }
        }

        grid_step(&g);
        cicli++;
        vivi = 0;
        for (int i = 0; i < rows * cols; i++) vivi |= g.cores[i].running;
    } while (vivi && cicli < MAX_CICLI);

    /* i core si fermano sulla ecall, ma l'ultimo valore esce dal bordo sud nel
       ciclo del commit successivo: un giro in piu' per raccoglierlo */
    for (int c = 0; c < cols; c++) {
        uint32_t v;
        if (grid_pop(&g, rows - 1, c, SUD, &v)) { assert(v == valore); usciti++; }
    }

    assert(cicli < MAX_CICLI);
    assert(!vivi);
    assert(usciti == cols);              /* uno per colonna, ne' perso ne' duplicato */

    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            assert(grid_at(&g, r, c) -> regs[S1] == valore);

    fprintf(stderr, "%-14s %dx%-3d ok  valore=%u  usciti=%d  cicli=%d\n",
            argv[1], rows, cols, valore, usciti, cicli);

    grid_free(&g);
    free(elf);
    return 0;
}
