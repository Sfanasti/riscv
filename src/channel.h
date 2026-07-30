#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdint.h>

/*

    Canale direzionato, profondità 1.
    OUT[d] del mittente == IN[opp(d)] del ricevente: i due lati dello stesso
    canale. Lo stato è codificato da due contatori mod 2:
        pieno / leggibile  :  wp != rp
        vuoto / scrivibile :  wp == rp

*/
typedef struct Channel {
    uint32_t data;       /* slot dati (un solo valore, profondità 1) */
    uint8_t  wp, rp;     /* write e read  pointer (mod 2) */

    uint32_t data_next;       /* stato successivo */
    uint8_t  wp_next, rp_next;

    /*

        OUT accettata ma non ancora pubblicata da SETRDY.
        Non ha un gemello _next e non passa dal commit: lo tocca solo il core
        proprietario (OUT e SETRDY), il consumatore non lo vede mai, quindi non
        c'è visibilità cross-core da proteggere.

    */
    uint8_t  pending;
} Channel;

/* le letture avvengono sempre e solo nello stato attuale */
static inline int ch_isrdy(const Channel *c) {
    return c -> wp != c -> rp;
}
static inline int ch_iswrt(const Channel *c) {
    return c -> wp == c -> rp;
}

/* SETRDY (produttore): se vuoto pubblica e ritorna 1, altrimenti 0. */
static inline int ch_setrdy(Channel *c) {
    if (c -> pending && ch_iswrt(c)) {
        c -> wp_next = c -> wp ^ 1;
        c -> pending = 0;
        return 1;
    }
    return 0;
}

/* Lettura consume-on-read: ritorna il dato e, se era pieno, libera lo slot.*/
static inline uint32_t ch_read_c(Channel *c) {
    uint32_t v = c -> data;
    if (ch_isrdy(c)) {
        c -> rp_next = c -> rp ^ 1;
    }
    return v;
}

/*

    Le scritture vanno SEMPRE nel next, mai nell'attuale.
    Il canale accetta al massimo una pubblicazione per ciclo: dopo il commit
    wp_next == wp sempre, quindi wp_next != wp significa "già impegnato in
    questo ciclo". Un core non ci arriva (esegue una istruzione per ciclo), ma
    l'host che alimenta il bordo può chiamare grid_push due volte di fila e
    senza questa guardia il primo valore sparirebbe senza dirlo a nessuno.

*/

static inline void ch_write(Channel *c, uint32_t v) {
    if (ch_iswrt(c) && c -> wp_next == c -> wp) {
        c -> data_next = v;
        c -> pending = 1;
    }
}

/* commit: un colpo solo, tutti e tre i campi insieme */
static inline void ch_commit(Channel *c) {
    c->data = c->data_next;
    c->wp = c->wp_next;
    c->rp = c->rp_next;
}

#endif
