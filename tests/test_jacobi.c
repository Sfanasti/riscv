/*
    Verifica di jacobi.s su griglia RxC. Il riferimento in C esegue le stesse
    operazioni intere del kernel, quindi il confronto è esatto. Senza CSV=1
    stampa max|u_k - u_(k-1)| per iterazione, da cui si sceglie ITER per una
    tolleranza data.
    Uso: test_jacobi <file.o> <righe> <colonne> <bordo> <iter> <seme>
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "risc.h"
#include "grid.h"
#include "elf.h"

#define MAX_CICLI 2000000   /* tetto: se lo tocca, il test fallisce */
#define S1 9                /* u, il valore della cella */

/* una iterazione come nel kernel: bordo al posto dei vicini esterni */
static void passo(const int32_t *u, int32_t *un, int rows, int cols, int32_t bordo) {
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int32_t n = (r == 0) ? bordo : u[(r - 1) * cols + c];
            int32_t s = (r == rows - 1) ? bordo : u[(r + 1) * cols + c];
            int32_t w = (c == 0) ? bordo : u[r * cols + (c - 1)];
            int32_t e = (c == cols - 1) ? bordo : u[r * cols + (c + 1)];
            un[r * cols + c] = (n + s + e + w + 2) >> 2;
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 7) {
        fprintf(stderr, "uso: %s <file.o> <righe> <colonne> <bordo> <iter> <seme>\n", argv[0]);
        return 1;
    }
    int     rows  = atoi(argv[2]);
    int     cols  = atoi(argv[3]);
    int32_t bordo = (int32_t)strtol(argv[4], NULL, 0);
    int     iter  = atoi(argv[5]);
    int     seme  = atoi(argv[6]);      /* 0: u=0, 1: u=r+c */
    assert(rows >= 1 && cols >= 1 && iter >= 1);

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

    int n = rows * cols;
    int cicli = 0, vivi, usciti = 0;
    do {
        /* contorno e drenaggio prima del passo, come farebbe un vicino */
        grid_border_fill(&g, (uint32_t)bordo);
        usciti += grid_border_drain(&g);

        grid_step(&g);
        cicli++;
        vivi = 0;
        for (int i = 0; i < n; i++) {
            vivi |= g.risc[i].running;
        }
    } while (vivi && cicli < MAX_CICLI);

    /* terminazione per ECALL, non per tetto (deadlock) */
    assert(cicli < MAX_CICLI);
    assert(!vivi);

    /* il riferimento, con la stessa inizializzazione del kernel */
    int32_t *u  = malloc((size_t)n * sizeof(int32_t));
    int32_t *un = malloc((size_t)n * sizeof(int32_t));
    assert(u && un);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            u[r * cols + c] = seme ? (int32_t)(r + c) : 0;
        }
    }

    /* tracce[k-1] = max|u_k - u_(k-1)| */
    int32_t *tracce = malloc((size_t)iter * sizeof(int32_t));
    assert(tracce);
    int32_t delta = 0;
    int     k_finale = 0;
    for (int k = 1; k <= iter; k++) {
        passo(u, un, rows, cols, bordo);
        delta = 0;
        for (int i = 0; i < n; i++) {
            int32_t d = un[i] - u[i];
            if (d < 0) {
                d = -d;
            }
            if (d > delta) {
                delta = d;
            }
        }
        for (int i = 0; i < n; i++) {
            u[i] = un[i];
        }
        tracce[k - 1] = delta;
        if (delta != 0) {
            k_finale = k;
        }
    }

    /* il campo dell'array è l'iterato ITER-esimo, esatto */
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            assert((int32_t)grid_at(&g, r, c) -> regs[S1] == u[r * cols + c]);
        }
    }

    /*
        il punto fisso deve essere la soluzione esatta, bordo ovunque; con la
        sola troncatura il campo si ferma sotto e l'asserzione fallisce
    */
    int convergente = (k_finale < iter);
    if (convergente) {
        for (int i = 0; i < n; i++) {
            assert(u[i] == bordo);
        }
    }

    /* delta = 0: k_finale è una convergenza, non l'ultima iterazione */
    unsigned ritentativi, attese;
    grid_spin(&g, &ritentativi, &attese);
    fprintf(stderr, "%d,%d,%d,%u,%u,%d,%d,%d,%d,%d,%d\n",
            rows, cols, cicli, ritentativi, attese,
            bordo, iter, seme, usciti, k_finale, convergente ? 0 : delta);

    /* traccia dei delta fino al primo zero, esclusa dal CSV */
    if (!getenv("CSV")) {
        fprintf(stderr, "%18s delta:", "");
        for (int k = 1; k <= iter && k <= k_finale + 1; k++) {
            fprintf(stderr, " %d", tracce[k - 1]);
        }
        fprintf(stderr, "\n");
    }

    free(tracce); free(u); free(un);
    grid_free(&g);
    free(elf);
    return 0;
}
