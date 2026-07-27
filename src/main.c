#include "elf.h"
#include "core.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define N 4
#define MAX_STEPS 200

uint32_t inOvest_vettore[MAX_STEPS];
uint32_t inEst_vettore[MAX_STEPS];

void run_single_core(RISCV_Core *core) {
    printf("\n[SINGLE CORE] Premi INVIO per ogni passo. Ctrl+C per uscire.\n");

    while (core->running) {
        printf("\nPremi INVIO per eseguire il prossimo step...");
        fflush(stdout);
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
        execute_step(core);
        print_state(core);
    }
    printf("\n[SINGLE CORE] Esecuzione terminata.\n");
}

void run_multi_core(RISCV_Core cluster[], int n, int max_steps) {
    int step = 0;
    int core_attivi = n;

    printf("\n[MULTI CORE] %d core in catena. Premi INVIO per ogni ciclo.\n", n);

    while (core_attivi > 0 && step < max_steps) {
        printf("\n\n");
        printf("       CICLO DI CLOCK    %d\n", step);
        printf("\n");

        /* Fase A - Snapshot degli ingressi */
        uint32_t snap_in[N][4];
        for (int i = 0; i < n; i++) {
            for (int d = 0; d < 4; d++) {
                snap_in[i][d] = cluster[i].in_reg[d];
            }
        }

        /* Fase di calcolo: ogni core esegue uno step */
        for (int i = 0; i < n; i++) {
            if (!cluster[i].running) continue;

            for (int d = 0; d < 4; d++) {
                cluster[i].in_reg[d] = snap_in[i][d];
            }

            execute_step(&cluster[i]);
        }

        /* Fase B - Propagazione lungo la catena cardinale E-W */
        for (int i = 0; i < n; i++) {

            if (i == 0) {
                cluster[i].in_reg[OVEST] = inOvest_vettore[step];
            } else {
                cluster[i].in_reg[OVEST] = cluster[i - 1].out_reg[EST];
            }

            if (i == n - 1) {
                cluster[i].in_reg[EST] = inEst_vettore[step];
            } else {
                cluster[i].in_reg[EST] = cluster[i + 1].out_reg[OVEST];
            }
        }

        // STAMPA interfacce
        printf("\nStato interfacce (dopo Fase B):\n");
        printf("%-8s | %-8s | %-8s | %-8s | %-8s\n",
               "Core", "IN_OV", "OUT_OV", "IN_EST", "OUT_EST");
        printf("---------|---------|---------|---------|--------\n");
        for (int i = 0; i < n; i++) {
            printf("Core[%d] | %-8d | %-8d | %-8d | %-8d\n",
                   i,
                   cluster[i].in_reg[OVEST],
                   cluster[i].out_reg[OVEST],
                   cluster[i].in_reg[EST],
                   cluster[i].out_reg[EST]);
        }

        printf("\nValori nel deposito di ogni core:\n");
        for (int j = 0; j < n; j++) {
            printf("Core[%d]: %-4d | ", j, (int32_t)cluster[j].regs[6]);
        }
        printf("\n");

        step++;
        printf("\nPremi INVIO per il prossimo ciclo...");
        fflush(stdout);
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
    }

    printf("\n[MULTI CORE] Simulazione terminata in %d cicli.\n", step);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Uso: %s <file.elf> [N_core]\n", argv[0]);
        return 1;
    }

    long fileSize;
    uint8_t *elf_content = load_elf(argv[1], &fileSize);
    if (!elf_content) return 1;
    check_elf(elf_content);
    Elf32_Ehdr *header = (Elf32_Ehdr *)elf_content;

    int n_core = (argc >= 3) ? atoi(argv[2]) : 1;
    printf("Entry point: 0x%08x | Core: %d\n", header->e_entry, n_core);

    if (n_core == 1) {
        RISCV_Core core;
        init_core(&core, header->e_entry, 0);
        carica_elf_in_core(&core, elf_content, header);
        run_single_core(&core);

    } else {
        if (n_core > N) {
            printf("Errore: massimo %d core supportati\n", N);
            free(elf_content);
            return 1;
        }

        RISCV_Core cluster[N];
        for (int i = 0; i < n_core; i++) {
            init_core(&cluster[i], header->e_entry, i);
            carica_elf_in_core(&cluster[i], elf_content, header);
            printf("Core[%d] inizializzato.\n", i);
        }

        uint32_t dati[] = {45, 12, 89, 7};
        int len = 4;
        int stride = 6;

        for (int s = 0; s < MAX_STEPS; s++) {
            if (s % stride == 0 && (s / stride) < len)
                inOvest_vettore[s] = dati[s / stride];
            else {
                inOvest_vettore[s] = 0xFFFFFFFF;
            }
            inEst_vettore[s] = 0xFFFFFFFF;
        }

        run_multi_core(cluster, n_core, MAX_STEPS);
    }

    free(elf_content);
    return 0;
}
