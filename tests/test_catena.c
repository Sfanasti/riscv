/*
    Verifica end-to-end dei programmi a catena (prodcons.s, chain.s) su 1xC.
    Uso: test_catena <file.o> <colonne> <somma attesa>
    Con <somma attesa> negativa la somma non viene verificata, solo misurata.

    Legge i registri direttamente invece di fare parsing della stampa. 
    Lo stesso binario serve entrambi i programmi, perché prodcons.s è il caso C=2 
    della stessa chain.
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "risc.h"
#include "grid.h"
#include "elf.h"

#define MAX_CICLI 1000000   /* tetto anti-deadlock: se lo tocca, il test fallisce */

/* indici dei registri usati dai programmi (si veda REG_NAMES in risc.c) */
#define A0 10   /* riga */
#define A1 11   /* colonna */
#define A2 12   /* righe totali */
#define A3 13   /* colonne totali */
#define S1  9   /* somma, sul sink */
#define S2 18   /* quanti ne restano da trattare */
#define S3 19   /* SETRDY rifiutate */
#define S4 20   /* ISRDY a vuoto */

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "uso: %s <file.o> <colonne> <somma attesa>\n", argv[0]);
        return 1;
    }
    int cols   = atoi(argv[2]);
    int attesa = atoi(argv[3]);
    assert(cols >= 2);

    long size;
    uint8_t *elf = load_elf(argv[1], &size);
    check_elf(elf, size);
    Elf32_Ehdr *h = (Elf32_Ehdr *)elf;

    Grid g;
    grid_init(&g, 1, cols, h -> e_entry);
    for (int c = 0; c < cols; c++) {
        carica_elf_in_risc(grid_at(&g, 0, c), elf, h, size);
    }

    /* la traccia per istruzione qui è rumore: il risultato esce su stderr */
    freopen("/dev/null", "w", stdout);

    int cicli = 0, vivi;
    do {
        grid_step(&g);
        cicli++;
        vivi = 0;
        for (int c = 0; c < cols; c++) {
            vivi |= grid_at(&g, 0, c) -> running;
        }
    } while (vivi && cicli < MAX_CICLI);

    /* terminazione: tutti fermi per ECALL, non per tetto raggiunto */
    assert(cicli < MAX_CICLI);
    assert(!vivi);

    unsigned reg_rit = 0, reg_att = 0;
    for (int c = 0; c < cols; c++) {
        RISC_V *k = grid_at(&g, 0, c);

        /* identità cablata: ogni cella sa dove si trova */
        assert(k -> regs[A0] == 0);
        assert((int)k -> regs[A1] == c);
        assert(k -> regs[A2] == 1);
        assert((int)k -> regs[A3] == cols);

        /* nessuno si è fermato a metà del proprio compito */
        assert(k -> regs[S2] == 0);

        reg_rit += k -> regs[S3];
        reg_att += k -> regs[S4];
    }

    /*
        Il conto del simulatore (grid_spin) e quello che il programma tiene in
        s3/s4 misurano la stessa cosa da due lati: ogni giro di respin esegue
        esattamente una ISRDY/SETRDY fallita. Devono coincidere in quanto rappresentano 
        la taratura dei contatori del simulatore, che gli altri kernel usano senza avere
        un equivalente in assembly da confrontare.
    */
    unsigned ritentativi, attese;
    grid_spin(&g, &ritentativi, &attese);
    assert(ritentativi == reg_rit);
    assert(attese == reg_att);

    /*
        il risultato, sull'ultima colonna. Si stampa sempre quello MISURATO:
        con attesa < 0 il test non verifica, misura e basta
        (NOBP=1, dove la somma sbagliata è il risultato che si vuole leggere)
    */
    int somma = (int)grid_at(&g, 0, cols - 1) -> regs[S1];
    if (attesa >= 0) {
        assert(somma == attesa);
    }

    /*
        Riga CSV: il prefisso (kernel e parametri) lo scrive il Makefile;
        qui si chiude con quello che sa solo il test.
    */
    fprintf(stderr, "1,%d,%s,%d,%u,%u,%d\n",
            cols, getenv("NOBP") ? "no" : "si", cicli, ritentativi, attese, somma);

    grid_free(&g);
    free(elf);
    return 0;
}
