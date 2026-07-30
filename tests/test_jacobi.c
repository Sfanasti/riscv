/* Verifica end-to-end di asm/jacobi.s (Jacobi a 5 punti) su griglia RxC.
   Uso: test_jacobi <file.o> <righe> <colonne> <bordo> <iter> <seme>

   L'asserzione principale e' esatta, non approssimata, e la ragione merita una
   riga: `srai` con l'arrotondamento e' aritmetica intera deterministica, non
   un'approssimazione della media. Quindi il riferimento in C non deve
   *modellare* il kernel, deve solo rifare le stesse operazioni sugli stessi
   int32_t — e il confronto e' bit per bit, senza tolleranze.

   Nota l'inversione: il riferimento ha i quattro rami sulla posizione
   (`r == 0 ? bordo : ...`) che l'assembly non ha. Quei rami sono la geometria,
   e sulla griglia vera la geometria sta nel cablaggio di grid_init piu'
   l'host che chiude i lati aperti, non nel programma.

   Da qui viene anche la tabella di convergenza: siccome il riferimento e'
   esatto, max|u_k - u_k-1| calcolato in C *e'* la convergenza dell'array, e
   non serve spiare i registri ciclo per ciclo. La tabella dice con quale ITER
   compilare per stare sotto una tolleranza data — il ciclo
   `while err > tol` di MATLAB, srotolato. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "core.h"
#include "grid.h"
#include "elf.h"

#define MAX_CICLI 2000000   /* tetto anti-deadlock: se lo tocca, il test fallisce */
#define S1 9                /* u, il valore della cella (vedi REG_NAMES in core.c) */

/* Una iterazione di Jacobi, identica a quella del kernel: la condizione al
   contorno sostituisce i vicini che cadono fuori, e il +2 prima dello shift e'
   l'arrotondamento al piu' vicino che evita il punto fisso spurio. */
static void passo(const int32_t *u, int32_t *un, int rows, int cols, int32_t bordo) {
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int32_t n = (r == 0)        ? bordo : u[(r - 1) * cols + c];
            int32_t s = (r == rows - 1) ? bordo : u[(r + 1) * cols + c];
            int32_t w = (c == 0)        ? bordo : u[r * cols + (c - 1)];
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
    int     seme  = atoi(argv[6]);      /* 0 = interno freddo, 1 = campo iniziale r+c */
    assert(rows >= 1 && cols >= 1 && iter >= 1);

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

    int n = rows * cols;
    int cicli = 0, vivi, usciti = 0;
    do {
        /* i due mestieri dell'host, nello stesso ordine di run_grid: alimentare
           il contorno e drenare il perimetro, entrambi PRIMA del passo, cosi'
           l'host paga la stessa latenza di un ciclo per hop di ogni cella */
        grid_border_fill(&g, (uint32_t)bordo);
        usciti += grid_border_drain(&g);

        grid_step(&g);
        cicli++;
        vivi = 0;
        for (int i = 0; i < n; i++) vivi |= g.cores[i].running;
    } while (vivi && cicli < MAX_CICLI);

    /* qui muore un deadlock, ed e' il rischio vero di questo kernel: una cella
       che aspetta un vicino in attesa a sua volta. Se l'ordine spedisci-tutti /
       ricevi-tutti venisse invertito nel .s, il test finisce qui. */
    assert(cicli < MAX_CICLI);
    assert(!vivi);

    /* il riferimento, con la stessa inizializzazione del kernel */
    int32_t *u  = malloc((size_t)n * sizeof(int32_t));
    int32_t *un = malloc((size_t)n * sizeof(int32_t));
    assert(u && un);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            u[r * cols + c] = seme ? (int32_t)(r + c) : 0;

    /* La tolleranza: max|u_k - u_k-1| a ogni iterazione, tracciata e stampata.
       Non asserita — e' la misura che dice quale ITER serve, e a renderla
       affidabile e' l'asserzione sotto, che lega il riferimento all'array.

       Attenzione a quale k si riporta. `delta` all'iterazione k e'
       max|u_k - u_k-1|, quindi va a zero la prima volta che un'iterazione non
       cambia niente: un giro DOPO che il campo ha raggiunto il suo valore
       definitivo. Il numero utile e' l'ultima iterazione che ha cambiato
       qualcosa, perche' quello e' l'ITER con cui compilare. */
    int32_t *tracce = malloc((size_t)iter * sizeof(int32_t));
    assert(tracce);
    int32_t delta = 0;
    int     k_finale = 0;
    for (int k = 1; k <= iter; k++) {
        passo(u, un, rows, cols, bordo);
        delta = 0;
        for (int i = 0; i < n; i++) {
            int32_t d = un[i] - u[i];
            if (d < 0) d = -d;
            if (d > delta) delta = d;
        }
        for (int i = 0; i < n; i++) u[i] = un[i];
        tracce[k - 1] = delta;
        if (delta != 0) k_finale = k;
    }

    /* L'ASSERZIONE: il campo dell'array e' l'iterato ITER-esimo, esatto */
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            assert((int32_t)grid_at(&g, r, c) -> regs[S1] == u[r * cols + c]);

    /* Se il riferimento ha raggiunto un punto fisso, quel punto fisso deve
       essere la soluzione vera: con contorno costante Jacobi converge a `bordo`
       su tutta la griglia, da qualunque campo iniziale. E' la verifica che
       l'arrotondamento serve a qualcosa — con `srai` secco il campo si ferma
       sotto `bordo` e questa asserzione fallisce. */
    int convergiuto = (k_finale < iter);
    if (convergiuto)
        for (int i = 0; i < n; i++) assert(u[i] == bordo);

    fprintf(stderr, "%-16s %dx%-3d bordo=%-5d iter=%-4d seme=%d  ok  cicli=%-7d usciti=%-6d",
            argv[1], rows, cols, bordo, iter, seme, cicli, usciti);
    if (convergiuto) fprintf(stderr, "  campo finale a k=%d\n", k_finale);
    else             fprintf(stderr, "  non ancora convergente (delta=%d)\n", delta);

    /* La traccia dei delta: da qui si legge il k per QUALUNQUE tolleranza,
       che e' il "while err > tol" srotolato. Stampata fino al primo zero,
       il resto sono iterazioni che non cambiano niente. */
    fprintf(stderr, "%18s delta:", "");
    for (int k = 1; k <= iter && k <= k_finale + 1; k++)
        fprintf(stderr, " %d", tracce[k - 1]);
    fprintf(stderr, "\n");

    free(tracce); free(u); free(un);
    grid_free(&g);
    free(elf);
    return 0;
}
