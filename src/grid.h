#ifndef GRID_H
#define GRID_H

#include "core.h"

typedef struct{
    int rows, cols;
    RISCV_Core *cores; // flat: rows*cols, cella (r,c) = cores[r*cols+c]
    Channel *border;   // canali di bordo: 2*(rows+cols), posseduti dalla griglia
}Grid;

void grid_init(Grid *grid, int rows, int cols, uint32_t start_pc);
void grid_free(Grid *grid);
void grid_step(Grid *grid);
RISCV_Core *grid_at(Grid *grid, int r, int c);

#endif