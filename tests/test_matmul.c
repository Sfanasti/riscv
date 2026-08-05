/*
    Verifica end-to-end di asm/matmul.s: C = A x B su griglia RxC.
    Uso: test_matmul <file.o> <righe> <colonne> <K>

    La cella (i,j) accumula C[i][j] e non si muove: a scorrere sono
    i dati, la riga i di A da OVEST verso EST e la colonna j di B da
    NORD verso SUD. Dopo K giri ogni cella ha visto esattamente gli 
    operandi del proprio prodotto interno.

    Quello che questo test dimostra:
      - il bordo alimentato in modo NON uniforme e variabile nel tempo, un
        valore diverso per riga e per colonna a ogni giro.Prima vera volta
        in cui si usa grid_push: BORDO=n e grid_border_fill fanno solo una
        costante uguale ovunque.
      - l'host NON sfalsa l'ingresso. In un systolic in lockstep a[i][k] deve
        entrare al ciclo i+k e b[k][j] al ciclo j+k, altrimenti la cella
        moltiplica la coppia sbagliata e il risultato è errato in silenzio.
        Qui si fa push() appena il canale accetta: la cella aspetta di avere
        entrambi gli operandi. Ready bit al posto della dipendenza dal tempo.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "risc.h"
#include "grid.h"
#include "elf.h"

#define MAX_CICLI 100000   /* tetto anti-deadlock: se lo tocca, il test fallisce */
#define S1  9              /* accumulatore C[i][j] (si veda REG_NAMES in risc.c) */
#define S2 18              /* termini rimasti: a fine corsa deve essere 0 */

/*
    Matrici deterministiche, calcolate invece che allocate. Non sono simmetriche
    di proposito: con A[i][k] = i+k un prodotto verrebbe uguale al suo trasposto
    e uno scambio righe/colonne nel cablaggio passerebbe inosservato.
*/
static int32_t elemA(int i, int k, int K)    { return i * K + k + 1; }
static int32_t elemB(int k, int j, int cols) { return k * cols + j + 1; }

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "uso: %s <file.o> <righe> <colonne> <K>\n", argv[0]);
        return 1;
    }
    int rows = atoi(argv[2]);
    int cols = atoi(argv[3]);
    int K    = atoi(argv[4]);       /* deve coincidere con il --defsym K del .s */
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
    freopen("/dev/null", "w", stdout);

    /*
        quanti termini sono già entrati da ogni lato: l'host non conta i cicli
        ma piuttosto i valori consegnati
    */
    int *ia = calloc((size_t)rows, sizeof(int));
    int *ib = calloc((size_t)cols, sizeof(int));
    int rifiutate = 0, cicli = 0, vivi;

    do {
        /*
            Alimentazione: si ritenta finché il canale non accetta, esattamente
            come farebbe una cella vicina. NOTA: un rifiuto non è un errore, è la
            backpressure vista dal lato host.
        */
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

        /*
            Drenaggio di est e sud: l'ultima colonna e l'ultima riga inoltrano
            comunque visto che il kernel è uniforme e nessuno sa di essere sul bordo,
            inoltre un OUT che nessuno consuma le inchioderebbe sulla propria SETRDY. 
            Non tocca l'alimentazione: grid_push scrive su in_ch, grid_pop legge da
            out_ch che sono canali diversi.
        */
        grid_border_drain(&g);

        grid_step(&g);
        cicli++;
        vivi = 0;
        for (int i = 0; i < rows * cols; i++) {
            vivi |= g.risc[i].running;
        }
    } while (vivi && cicli < MAX_CICLI);

    /* controllo del deadlock */
    assert(cicli < MAX_CICLI);
    assert(!vivi);

    /*
        tutti i termini consegnati: se l'host è rimasto con dei valori in mano,
        qualcuno ha smesso di leggere prima della fine
    */
    for (int i = 0; i < rows; i++) {
        assert(ia[i] == K);
    }
    for (int j = 0; j < cols; j++) {
        assert(ib[j] == K);
    }

    /*  ogni cella ha il proprio elemento di C, non un altro */
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            int32_t atteso = 0;
            for (int k = 0; k < K; k++) {
                atteso += elemA(i, k, K) * elemB(k, j, cols);
            }

            RISC_V *cella = grid_at(&g, i, j);
            assert(cella -> regs[S2] == 0);                    /* K giri fatti tutti */
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
