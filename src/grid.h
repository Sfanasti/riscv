#ifndef GRID_H
#define GRID_H

#include "risc.h"

typedef struct{
    int rows, cols;
    RISC_V *risc; /* rows*cols celle, (r,c) = risc[r*cols+c] */
    Channel *border;   /* 2*(rows+cols) canali di bordo */
}Grid;

void grid_init(Grid *grid, int rows, int cols, uint32_t start_pc);
void grid_free(Grid *grid);
void grid_step(Grid *grid);
RISC_V *grid_at(Grid *grid, int r, int c);

/* SETRDY rifiutate e ISRDY a vuoto, sommate su tutte le celle */
void grid_spin(const Grid *grid, unsigned *ritentativi, unsigned *attese);

/* I/O di bordo; push e pop: 1 = pubblicato / letto, 0 = pieno / vuoto */
int  grid_push(Grid *grid, int r, int c, int dir, uint32_t v);
int  grid_pop (Grid *grid, int r, int c, int dir, uint32_t *v);
void grid_border_fill(Grid *grid, uint32_t v);
int  grid_border_drain(Grid *grid);

#endif
