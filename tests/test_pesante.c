/*
    Verifica di pesante.s su griglia RxC. Il riferimento in C esegue le stesse
    operazioni su uint32_t (srli è logico, l'overflow senza segno è definito),
    quindi il confronto è esatto bit per bit.
    Uso: test_pesante <file.o> <righe> <colonne> <bordo> <iter> <peso>
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "risc.h"
#include "grid.h"
#include "elf.h"

#define MAX_CICLI 2000000   /* tetto: se lo tocca, il test fallisce */
#define S1 9                /* il valore della cella */

/*
    terminazione controllata ogni BLOCCO cicli: la scansione è seriale. I
    cicli riportati sono arrotondati per eccesso a un multiplo di BLOCCO.
*/
#define BLOCCO 64

/* xorshift (13, 17, 5) più costante additiva, come nel kernel */
static uint32_t mescola(uint32_t x, int peso) {
    for (int p = 0; p < peso; p++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        x += 0x9E3779B9u;
    }
    return x;
}

/* una iterazione come nel kernel: bordo al posto dei vicini esterni */
static void passo(const uint32_t *u, uint32_t *un, int rows, int cols,
                  uint32_t bordo, int peso) {
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            uint32_t n = (r == 0)        ? bordo : u[(r - 1) * cols + c];
            uint32_t s = (r == rows - 1) ? bordo : u[(r + 1) * cols + c];
            uint32_t w = (c == 0)        ? bordo : u[r * cols + (c - 1)];
            uint32_t e = (c == cols - 1) ? bordo : u[r * cols + (c + 1)];
            un[r * cols + c] = mescola(n + s + e + w, peso);
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 7) {
        fprintf(stderr, "uso: %s <file.o> <righe> <colonne> <bordo> <iter> <peso>\n", argv[0]);
        return 1;
    }
    int      rows  = atoi(argv[2]);
    int      cols  = atoi(argv[3]);
    uint32_t bordo = (uint32_t)strtoul(argv[4], NULL, 0);
    int      iter  = atoi(argv[5]);
    int      peso  = atoi(argv[6]);
    assert(rows >= 1 && cols >= 1 && iter >= 1 && peso >= 1);

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
    int cicli = 0, vivi = 1, usciti = 0;
    do {
        for (int b = 0; b < BLOCCO && cicli < MAX_CICLI; b++) {
            /* contorno e drenaggio prima del passo, come farebbe un vicino */
            grid_border_fill(&g, bordo);
            usciti += grid_border_drain(&g);

            grid_step(&g);
            cicli++;
        }
        vivi = 0;
        for (int i = 0; i < n; i++) {
            vivi |= g.risc[i].running;
        }
    } while (vivi && cicli < MAX_CICLI);

    /* terminazione per ECALL, non per tetto (deadlock) */
    assert(cicli < MAX_CICLI);
    assert(!vivi);

    /* il riferimento, con la stessa inizializzazione del kernel */
    uint32_t *u  = malloc((size_t)n * sizeof(uint32_t));
    uint32_t *un = malloc((size_t)n * sizeof(uint32_t));
    assert(u && un);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            u[r * cols + c] = (uint32_t)(r + c);
        }
    }
    for (int k = 0; k < iter; k++) {
        passo(u, un, rows, cols, bordo, peso);
        for (int i = 0; i < n; i++) {
            u[i] = un[i];
        }
    }

    /* il campo dell'array è l'iterato ITER-esimo, esatto */
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            assert(grid_at(&g, r, c) -> regs[S1] == u[r * cols + c]);
        }
    }

    unsigned ritentativi, attese;
    grid_spin(&g, &ritentativi, &attese);
    fprintf(stderr, "%d,%d,%d,%u,%u,%u,%d,%d,%d\n",
            rows, cols, cicli, ritentativi, attese,
            bordo, iter, peso, usciti);

    free(u); free(un);
    grid_free(&g);
    free(elf);
    return 0;
}
