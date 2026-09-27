#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <omp.h>

#define N      20000   /* aperture per ripetizione */
#define RIP    7       /* ripetizioni, se ne prende il minimo */
#define STRIDE 16632   /* sizeof(RISC_V): distanza fra due celle contigue */
#define PASSO  16      /* long per thread: una linea di cache fra due slot */

/* un'area per thread, distanziate di una linea di cache: il corpo impedisce
   al compilatore di eliminare la regione senza introdurre contesa */
static long slot[64 * PASSO];

static double ora(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* microsecondi per apertura di una regione quasi vuota */
static double regione(int thr) {
    double best = 1e9;
    for (int r = 0; r < RIP; r++) {
        double t0 = ora();
        for (int i = 0; i < N; i++) {
            #pragma omp parallel num_threads(thr)
            {
                slot[omp_get_thread_num() * PASSO] += 1;
            }
        }
        double t = (ora() - t0) / N;
        if (t < best) {
            best = t;
        }
    }
    return best * 1e6;
}

/* microsecondi per ciclo di clock: le due regioni di grid_step su lato*lato
   celle, ciascuna iterazione sulla propria cella */
static double due_regioni(int lato, int thr, int cicli) {
    size_t n = (size_t)lato * lato;
    size_t byte = ((n * STRIDE) / 4096 + 1) * 4096;
    uint8_t *m = aligned_alloc(4096, byte);
    if (m == NULL) {
        return -1.0;
    }
    memset(m, 1, byte);

    double best = 1e9;
    for (int r = 0; r < RIP; r++) {
        double t0 = ora();
        for (int k = 0; k < cicli; k++) {
            /* fase di calcolo: legge dalla cella, scrive in un campo _next */
            #pragma omp parallel for num_threads(thr) schedule(static)
            for (size_t i = 0; i < n; i++) {
                uint8_t *c = m + i * STRIDE;
                *(uint32_t *)(c + 16532) = *(uint32_t *)(c + 132) + (uint32_t)k;
            }
            /* fase di aggiornamento: il campo _next diventa attuale */
            #pragma omp parallel for num_threads(thr) schedule(static)
            for (size_t i = 0; i < n; i++) {
                uint8_t *c = m + i * STRIDE;
                *(uint32_t *)(c + 16528) = *(uint32_t *)(c + 16532);
            }
        }
        double t = (ora() - t0) / cicli;
        if (t < best) {
            best = t;
        }
    }
    free(m);
    return best * 1e6;
}

int main(void) {
    printf("apertura di una regione parallela (minimo di %d ripetizioni, "
           "%d aperture ciascuna)\n", RIP, N);
    int thr[] = {2, 8, 16};
    for (int i = 0; i < 3; i++) {
        printf("  %2d thread: %6.2f us\n", thr[i], regione(thr[i]));
    }

    printf("\ndue regioni per ciclo di clock, 8 thread, celle da %d byte\n",
           STRIDE);
    int lati[]  = {12, 32, 64, 128};
    int cicli[] = {2000, 1000, 500, 200};
    for (int i = 0; i < 4; i++) {
        printf("  %3dx%-3d: %6.1f us\n", lati[i], lati[i],
               due_regioni(lati[i], 8, cicli[i]));
    }
    return 0;
}
