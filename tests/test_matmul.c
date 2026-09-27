/*
    Verifica di matmul.s, C = A x B su griglia RxC. L'host spinge con grid_push
    la riga i di A a OVEST e la colonna j di B a NORD, appena il canale
    accetta e senza sfasare gli ingressi.
    Uso: test_matmul <file.o> <righe> <colonne> <K>
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "risc.h"
#include "grid.h"
#include "elf.h"

#define MAX_CICLI 100000   /* tetto: se lo tocca, il test fallisce */
#define S1  9              /* accumulatore C[i][j] */
#define S2 18              /* termini rimasti: a fine corsa deve essere 0 */

/* A e B non simmetriche: uno scambio righe/colonne non passa inosservato */
static int32_t elemA(int i, int k, int K)    { return i * K + k + 1; }
static int32_t elemB(int k, int j, int cols) { return k * cols + j + 1; }

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "uso: %s <file.o> <righe> <colonne> <K>\n", argv[0]);
        return 1;
    }
    int rows = atoi(argv[2]);
    int cols = atoi(argv[3]);
    int K    = atoi(argv[4]);       /* = --defsym K del .s */
    assert(rows >= 1 && cols >= 1 && K >= 1);

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

    /* termini già consegnati per riga di A e per colonna di B */
    int *ia = calloc((size_t)rows, sizeof(int));
    int *ib = calloc((size_t)cols, sizeof(int));
    int rifiutate = 0, cicli = 0, vivi;

    do {
        /* un rifiuto è backpressure: si ritenta al ciclo dopo */
        for (int i = 0; i < rows; i++) {
            if (ia[i] < K) {
                if (grid_push(&g, i, 0, OVEST, (uint32_t)elemA(i, ia[i], K))) {
                    ia[i]++;
                } else {
                    rifiutate++;
                }
            }
        }
        for (int j = 0; j < cols; j++) {
            if (ib[j] < K) {
                if (grid_push(&g, 0, j, NORD, (uint32_t)elemB(ib[j], j, cols))) {
                    ib[j]++;
                } else {
                    rifiutate++;
                }
            }
        }

        /* l'ultima riga e l'ultima colonna inoltrano comunque: si drenano */
        grid_border_drain(&g);

        grid_step(&g);
        cicli++;
        vivi = 0;
        for (int i = 0; i < rows * cols; i++) {
            vivi |= g.risc[i].running;
        }
    } while (vivi && cicli < MAX_CICLI);

    /* terminazione per ECALL, non per tetto */
    assert(cicli < MAX_CICLI);
    assert(!vivi);

    /* tutti i termini consegnati */
    for (int i = 0; i < rows; i++) {
        assert(ia[i] == K);
    }
    for (int j = 0; j < cols; j++) {
        assert(ib[j] == K);
    }

    /* ogni cella ha il proprio elemento di C */
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            int32_t atteso = 0;
            for (int k = 0; k < K; k++) {
                atteso += elemA(i, k, K) * elemB(k, j, cols);
            }

            RISC_V *cella = grid_at(&g, i, j);
            assert(cella -> regs[S2] == 0);    /* K giri fatti */
            assert((int32_t)cella -> regs[S1] == atteso);
        }
    }

    unsigned ritentativi, attese;
    grid_spin(&g, &ritentativi, &attese);
    fprintf(stderr, "%d,%d,%d,%u,%u,%d\n",
            rows, cols, cicli, ritentativi, attese, rifiutate);

    free(ia); free(ib);
    grid_free(&g);
    free(elf);
    return 0;
}
