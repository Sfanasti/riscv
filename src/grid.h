#ifndef GRID_H
#define GRID_H

#include "core.h"

typedef struct{
    int rows, cols;
    RISC_V *cores; // flat: rows*cols, cella (r,c) = cores[r*cols+c]
    Channel *border;   // canali di bordo: 2*(rows+cols), posseduti dalla griglia
}Grid;

void grid_init(Grid *grid, int rows, int cols, uint32_t start_pc);
void grid_free(Grid *grid);
void grid_step(Grid *grid);
RISC_V *grid_at(Grid *grid, int r, int c);

/* I/O di bordo: l'host e' il vicino che le celle di perimetro non hanno.
   Vanno chiamate PRIMA di grid_step, nella stessa finestra in cui i core
   calcolano: come per loro le scritture finiscono nel "next" e diventano
   visibili al commit, quindi anche l'host paga un ciclo di latenza per hop.
   (r,c,dir) deve puntare fuori dalla griglia, altrimenti si starebbe
   scavalcando un vicino vero. */
int  grid_push(Grid *grid, int r, int c, int dir, uint32_t v);   /* -> 1 se pubblicato, 0 se lo slot e' ancora pieno */
int  grid_pop (Grid *grid, int r, int c, int dir, uint32_t *v);  /* -> 1 se c'era un dato, 0 se il canale e' vuoto  */
void grid_border_fill(Grid *grid, uint32_t v);                   /* condizione al contorno costante su tutto il perimetro */
int  grid_border_drain(Grid *grid);                              /* -> quanti valori sono usciti; scartati */

#endif