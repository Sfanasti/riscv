/*
    clock_gettime e CLOCK_MONOTONIC sono POSIX, e -std=c11 (senza gnu) li
    nasconde: la macro li riporta a galla senza dover cambiare CFLAGS per
    tutto il progetto. Serve clock_gettime e non clock(): quest'ultima misura
    tempo di CPU, che con piu' thread e' la somma dei thread e non dice
    niente sullo speedup.
*/
#define _POSIX_C_SOURCE 199309L

#include "grid.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _OPENMP
#include <omp.h>
#endif

/*
    Cronometro della simulazione, acceso da TEMPI=<file>: senza quella
    variabile grid_step paga un solo test su un puntatore e non si scrive
    niente, quindi make test e make dati restano identici a prima.

    Non si usa "time" della shell perché quello misurerebbe anche il
    caricamento dell'ELF e il riferimento in C degli harness (in test_jacobi
    sono ITER passi su tutta la griglia), che non fanno parte della
    simulazione. Qui il cronometro parte al primo grid_step e si ferma
    all'ultimo.

    Si misurano DUE tempi, non uno:

        t_step   tempo speso dentro grid_step, cioè la parte parallelizzata
        t_tot    dal primo grid_step all'ultimo, quindi anche l'I/O di bordo e
                 il controllo di terminazione dell'harness, che sono seriali

    Il loro rapporto dà 1 - t_step/t_tot, la frazione seriale MISURATA: è il
    tetto di Amdahl del simulatore. Serve perché il tempo totale da solo non
    dice se uno speedup deludente venga dalla griglia o dal driver che la usa,
    e in questo progetto viene dal driver.

    Costo della misura: due clock_gettime per ciclo, circa 40 ns. Trascurabile
    sulle griglie grandi, ma su una 1x1 è dello stesso ordine del lavoro di un
    ciclo: quelle righe vanno lette come ordine di grandezza, non come misura.
*/
static const char *file_tempi;              /* NULL = cronometro spento */
static const char *nome_kernel;
static double      t_step, t_prima, t_ultima;
static long        n_step;
static int         t_righe, t_colonne;

static double ora(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int quanti_thread(void) {
#ifdef _OPENMP
    return omp_get_max_threads();
#else
    return 1;   /* build senza -fopenmp: le pragma sono commenti */
#endif
}

static void scrivi_tempi(void) {
    double t_tot = t_ultima - t_prima;

    FILE *f = fopen(file_tempi, "a");
    if (f == NULL) {
        return;
    }
    fprintf(f, "%s,%d,%d,%d,%ld,%.6f,%.6f,%.4f\n",
            nome_kernel, t_righe, t_colonne, quanti_thread(), n_step,
            t_tot, t_step, t_tot > 0.0 ? 1.0 - t_step / t_tot : 0.0);
    fclose(f);
}

/*
    Chiamata da grid_init, che gira sempre da codice seriale prima di
    qualunque regione parallela: stessa convenzione di leggi_ambiente in
    risc.c. La riga esce da atexit, così gli harness non cambiano di una riga
    e un run che fallisce un assert non lascia un tempo nel CSV.
*/
static void tempi_init(const Grid *grid) {
    static int registrato;

    file_tempi = getenv("TEMPI");
    if (file_tempi == NULL) {
        return;
    }
    nome_kernel = getenv("KERNEL") != NULL ? getenv("KERNEL") : "?";
    t_righe     = grid -> rows;
    t_colonne   = grid -> cols;
    t_step      = 0.0;
    n_step      = 0;

    if (!registrato) {
        atexit(scrivi_tempi);
        registrato = 1;
    }
}

void grid_init(Grid *grid, int rows, int cols, uint32_t start_pc){
     if (rows <= 0 || cols <= 0) {
        fprintf(stderr, "griglia %dx%d: righe e colonne devono essere > 0\n", rows, cols);
        exit(1);
    }

    int n_border= 2 *(rows + cols);

    grid -> rows = rows;
    grid -> cols = cols;
    grid -> risc = malloc((size_t)rows * cols * sizeof(RISC_V));
    grid -> border = calloc((size_t)n_border, sizeof(Channel));

    for(int r = 0; r < rows; r++){
        for(int c = 0; c < cols; c++){
            RISC_V *k = grid_at(grid, r, c);
            init_risc(k, start_pc, r * cols + c);

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

    tempi_init(grid);
}

void grid_free(Grid *grid){
    free(grid -> risc);
    free(grid -> border);
    grid -> risc = NULL;
    grid -> border = NULL;
}

void grid_step(Grid *grid) {
    int n = grid -> rows * grid -> cols;
    double t0 = 0.0;

    if (file_tempi != NULL) {
        t0 = ora();
        if (n_step == 0) {
            t_prima = t0;
        }
    }

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; i++){                 /* fase compute */
        if (grid -> risc[i].running) {
            execute_step(&grid -> risc[i]);
        }
    }

    /* barriera implicita a fine loop: nessuno committa prima che tutti
       abbiano calcolato. È esattamente la separazione delle due fasi. */

    #pragma omp parallel for schedule(static)
    for (int i = 0; i < n; i++){                  /* fase commit */
        for (int d = 0; d < 4; d++){
            ch_commit(&grid -> risc[i].out_ch[d]);
        }
    }

    for (int i = 0; i < 2 * (grid->rows + grid->cols); i++) {
        ch_commit(&grid->border[i]);
    }

    if (file_tempi != NULL) {
        t_ultima = ora();
        t_step  += t_ultima - t0;
        n_step++;
    }
}


RISC_V *grid_at(Grid *grid, int r, int c){
    return &grid -> risc[r * grid -> cols + c];
}

void grid_spin(const Grid *grid, unsigned *ritentativi, unsigned *attese){
    *ritentativi = 0;
    *attese = 0;
    for (int i = 0; i < grid -> rows * grid -> cols; i++) {
        *ritentativi += grid -> risc[i].ritentativi;
        *attese      += grid -> risc[i].attese;
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