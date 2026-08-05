#include "elf.h"
#include "risc.h"
#include "grid.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static int grid_any_running(Grid *g) {
    for (int i = 0; i < g->rows * g->cols; i++) {
        if (g->risc[i].running) {
            return 1;
        }
    }
    return 0;
}

static void wait_enter(void) {
    printf("\nPremi INVIO per il prossimo ciclo...");
    fflush(stdout);
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

void run_single_risc(RISC_V *risc) {
    printf("\n[SINGLE RISC] Premi INVIO per ogni passo. Ctrl+C per uscire.\n");

    while (risc -> running) {
        wait_enter();
        execute_step(risc);
        print_state(risc);
    }
    printf("\n[SINGLE RISC] Esecuzione terminata.\n");
}

static void grid_print(Grid *grid) {
    for (int r = 0; r < grid->rows; r++) {
        for (int c = 0; c < grid->cols; c++) {
            print_state(grid_at(grid, r, c));
        }
    }
}

void run_grid(Grid *grid, int cicli) {
    int passo_passo = getenv("STEP") != NULL;   /* STEP=1 -> un ciclo per INVIO */
    const char *bordo = getenv("BORDO");        /* BORDO=<n> -> contorno costante */
    int step = 0, usciti = 0;

    while (step < cicli && grid_any_running(grid)) {
        if (passo_passo) {
            printf("\n\n       CICLO DI CLOCK    %d\n\n", step);
            grid_print(grid);
            wait_enter();
        }

        /*
            L'host prima del passo: alimenta il perimetro. Il contorno costante
            è il caso dello stencil (Dirichlet); un contorno che varia per
            cella o nel tempo si scrive con grid_push, come si vede in tests/test_bordo.c.
            Senza BORDO i canali di bordo restano vuoti per sempre, cioè un
            bordo aperto da cui non arriva mai niente.
        */
        if (bordo) {
            grid_border_fill(grid, (uint32_t)strtoul(bordo, NULL, 0));
        }

        /*
            Il drenaggio non è opzionale come l'alimentazione: un OUT di
            perimetro che nessuno consuma inchioda la cella sulla propria
            SETRDY. Si stampa solo il totale: un kernel come lo stencil, che
            spinge fuori da tutti e quattro i lati a ogni iterazione, sommergerebbe
            la traccia. Per un valore preciso c'è grid_pop da C.
        */
        usciti += grid_border_drain(grid);

        grid_step(grid);
        step++;
    }

    printf("\n[GRID] fermata dopo %d cicli (%s), %d valori usciti dal perimetro\n",
           step, grid_any_running(grid) ? "limite cicli raggiunto" : "tutti i RISC fermi",
           usciti);
    grid_print(grid);
}

int main(int argc, char **argv) {
    if (argc != 2 && argc != 5) {
        printf("Debug singolo RISC: %s <file.elf>\n", argv[0]);
        printf("Griglia:            %s <file.elf> <rows> <cols> <cicli>\n", argv[0]);
        return 1;
    }

    long fileSize;
    uint8_t *elf_content = load_elf(argv[1], &fileSize);
    if (!elf_content) {
        return 1;
    }
    check_elf(elf_content, fileSize);
    Elf32_Ehdr *header = (Elf32_Ehdr *)elf_content;
    printf("Entry point: 0x%08x\n", header -> e_entry);

    if (argc == 2) {
        RISC_V risc;
        init_risc(&risc, header -> e_entry, 0);
        carica_elf_in_risc(&risc, elf_content, header, fileSize);
        run_single_risc(&risc);

    } else {
        int rows  = atoi(argv[2]);
        int cols  = atoi(argv[3]);
        int cicli = atoi(argv[4]);

        Grid grid;
        grid_init(&grid, rows, cols, header -> e_entry);
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                carica_elf_in_risc(grid_at(&grid, r, c), elf_content, header, fileSize);
            }
        }

        run_grid(&grid, cicli);
        grid_free(&grid);
    }

    free(elf_content);
    return 0;
}
