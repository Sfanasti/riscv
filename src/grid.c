#include "grid.h"
#include <stdio.h>
#include <stdlib.h>

void grid_init(Grid *grid, int rows, int cols, uint32_t start_pc){
    int n_border= 2 *(rows + cols);
    
    grid -> rows = rows;
    grid -> cols = cols;
    grid -> cores = malloc((size_t)rows * cols * sizeof(RISCV_Core));
    grid -> border = calloc((size_t)n_border, sizeof(Channel));

    for(int r = 0; r < rows; r++){
        for(int c = 0; c < cols; c++){
            RISCV_Core *k = grid_at(grid, r, c);
            init_core(k, start_pc, r * cols + c);

            /* Identita' cablata: ogni cella nasce sapendo dove si trova.
               Serve perche' i programmi scelgono il proprio ruolo dalla
               posizione (sono il bordo ovest? l'ultima colonna?) e ricavarla
               dall'id lineare richiederebbe una divisione, che rv32i non ha.
               E' l'equivalente delle coordinate cablate di una cella systolic
               vera; su hardware RISC-V standard sarebbe il CSR mhartid. */
            k -> regs[10] = (uint32_t)r;      // a0 = riga
            k -> regs[11] = (uint32_t)c;      // a1 = colonna
            k -> regs[12] = (uint32_t)rows;   // a2 = righe totali
            k -> regs[13] = (uint32_t)cols;   // a3 = colonne totali
        }
    }

    //In orizzontale E <-> O: (r,c) <-> (r,c±1)
    for(int r = 0; r < rows; r++){
        for(int c = 0; c < cols - 1; c++){
            RISCV_Core *a = grid_at(grid, r, c);
            RISCV_Core *b = grid_at(grid, r, c + 1);
            b -> in_ch[OVEST] = &a -> out_ch[EST];
            a -> in_ch[EST] = &b -> out_ch[OVEST];
        }
    }

    //In verticale S <-> N: (r,c) <-> (r±1,c1)
    for(int r = 0; r < rows - 1; r++){
        for(int c = 0; c < cols; c++){
            RISCV_Core *a = grid_at(grid, r, c);
            RISCV_Core *b = grid_at(grid, r + 1, c);
            b -> in_ch[NORD] = &a -> out_ch[SUD];
            a -> in_ch[SUD] = &b -> out_ch[NORD];
        }
    }

    int idx = 0;
    for (int c = 0; c < cols; c++) {
        grid_at(grid, 0, c)->in_ch[NORD] = &grid->border[idx++];
        grid_at(grid, rows - 1, c)->in_ch[SUD] = &grid->border[idx++];
    }
    
    for (int r = 0; r < rows; r++) {
        grid_at(grid, r, 0)->in_ch[OVEST] = &grid->border[idx++];
        grid_at(grid, r, cols - 1)->in_ch[EST] = &grid->border[idx++];
    }
}

void grid_free(Grid *grid){
    free(grid -> cores);
    free(grid -> border);
    grid -> cores = NULL;
    grid -> border = NULL;
}

void grid_step(Grid *grid) {
    int n = grid -> rows * grid -> cols;

    for (int i = 0; i < n; i++){                 // fase compute
        if (grid -> cores[i].running) execute_step(&grid -> cores[i]);
    }

    for (int i = 0; i < n; i++){                  // fase commit
        for (int d = 0; d < 4; d++){
            ch_commit(&grid -> cores[i].out_ch[d]);
        }
    }
    
    for (int i = 0; i < 2 * (grid->rows + grid->cols); i++) {
        ch_commit(&grid->border[i]);
    }
}


RISCV_Core *grid_at(Grid *grid, int r, int c){
    return &grid -> cores[r * grid -> cols + c];
}