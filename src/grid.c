#include "grid.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void grid_init(Grid *grid, int rows, int cols, uint32_t start_pc){
     if (rows <= 0 || cols <= 0) {
        fprintf(stderr, "griglia %dx%d: righe e colonne devono essere > 0\n", rows, cols);
        exit(1);
    }

    int n_border= 2 *(rows + cols);

    grid -> rows = rows;
    grid -> cols = cols;
    grid -> cores = malloc((size_t)rows * cols * sizeof(RISC_V));
    grid -> border = calloc((size_t)n_border, sizeof(Channel));

    for(int r = 0; r < rows; r++){
        for(int c = 0; c < cols; c++){
            RISC_V *k = grid_at(grid, r, c);
            init_core(k, start_pc, r * cols + c);

            /*

                Identità cablata: ogni cella nasce sapendo dove si trova.
                Serve perché i programmi scelgono il proprio ruolo dalla
                posizione e ricavarla dall'id lineare richiederebbe una divisione,
                che rv32i non ha.

            */
            k -> regs[10] = (uint32_t)r;      /* a0 = riga */
            k -> regs[11] = (uint32_t)c;      /* a1 = colonna */
            k -> regs[12] = (uint32_t)rows;   /* a2 = righe totali */
            k -> regs[13] = (uint32_t)cols;   /* a3 = colonne totali */
        }
    }

    /* In orizzontale E <-> O: (r,c) <-> (r,c±1) */
    for(int r = 0; r < rows; r++){
        for(int c = 0; c < cols - 1; c++){
            RISC_V *a = grid_at(grid, r, c);
            RISC_V *b = grid_at(grid, r, c + 1);
            b -> in_ch[OVEST] = &a -> out_ch[EST];
            a -> in_ch[EST] = &b -> out_ch[OVEST];
        }
    }

    /* In verticale S <-> N: (r,c) <-> (r±1,c1) */
    for(int r = 0; r < rows - 1; r++){
        for(int c = 0; c < cols; c++){
            RISC_V *a = grid_at(grid, r, c);
            RISC_V *b = grid_at(grid, r + 1, c);
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

    for (int i = 0; i < n; i++){                 /* fase compute */
        if (grid -> cores[i].running) {
            execute_step(&grid -> cores[i]);
        }
    }

    for (int i = 0; i < n; i++){                  /* fase commit */
        for (int d = 0; d < 4; d++){
            ch_commit(&grid -> cores[i].out_ch[d]);
        }
    }

    for (int i = 0; i < 2 * (grid->rows + grid->cols); i++) {
        ch_commit(&grid->border[i]);
    }
}


RISC_V *grid_at(Grid *grid, int r, int c){
    return &grid -> cores[r * grid -> cols + c];
}

void grid_spin(const Grid *grid, unsigned *ritentativi, unsigned *attese){
    *ritentativi = 0;
    *attese = 0;
    for (int i = 0; i < grid -> rows * grid -> cols; i++) {
        *ritentativi += grid -> cores[i].ritentativi;
        *attese      += grid -> cores[i].attese;
    }
}

/*

    Una direzione è "di bordo" esattamente quando il suo IN è uno dei canali
    posseduti dalla griglia: il cablaggio di grid_init lo ha già deciso, non
    serve rifare il conto sulle coordinate.

*/
static int e_bordo(Grid *grid, int r, int c, int dir){
    Channel *ch = grid_at(grid, r, c) -> in_ch[dir];
    return ch >= grid -> border && ch < grid -> border + 2 * (grid -> rows + grid -> cols);
}

int grid_push(Grid *grid, int r, int c, int dir, uint32_t v){
    assert(e_bordo(grid, r, c, dir));

    /*

        l'host fa quello che farebbe un vicino produttore: OUT poi SETRDY.
        Se la cella non ha ancora consumato il valore precedente la ch_write
        viene rifiutata, ch_setrdy dà 0 e il chiamante ritenta: stessa
        backpressure che si vede fra due celle.

    */
    Channel *ch = grid_at(grid, r, c) -> in_ch[dir];
    ch_write(ch, v);
    return ch_setrdy(ch);
}

int grid_pop(Grid *grid, int r, int c, int dir, uint32_t *v){
    assert(e_bordo(grid, r, c, dir));

    /*

        qui l'host è il consumatore: senza questa lettura l'OUT di perimetro
        resta pieno per sempre e la cella si blocca sulla propria SETRDY.

    */
    Channel *ch = &grid_at(grid, r, c) -> out_ch[dir];
    if (!ch_isrdy(ch)) {
        return 0;
    }
    *v = ch_read_c(ch);
    return 1;
}

void grid_border_fill(Grid *grid, uint32_t v){
    for (int c = 0; c < grid -> cols; c++) {
        grid_push(grid, 0, c, NORD, v);
        grid_push(grid, grid -> rows - 1, c, SUD, v);
    }
    for (int r = 0; r < grid -> rows; r++) {
        grid_push(grid, r, 0, OVEST, v);
        grid_push(grid, r, grid -> cols - 1, EST, v);
    }
}

/*

    Speculare a grid_border_fill:
    condizione affinché un kernel che spinge fuori dal perimetro possa
    terminare. I valori si scartano : chi ne vuole uno preciso usa grid_pop
    sulla direzione che gli interessa

*/
int grid_border_drain(Grid *grid){
    int usciti = 0;
    uint32_t v;

    for (int c = 0; c < grid -> cols; c++) {
        usciti += grid_pop(grid, 0, c, NORD, &v);
        usciti += grid_pop(grid, grid -> rows - 1, c, SUD, &v);
    }
    for (int r = 0; r < grid -> rows; r++) {
        usciti += grid_pop(grid, r, 0, OVEST, &v);
        usciti += grid_pop(grid, r, grid -> cols - 1, EST, &v);
    }
    return usciti;
}