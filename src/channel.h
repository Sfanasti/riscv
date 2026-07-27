#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdint.h>

/* Canale direzionato, profondita' 1 (spec section 4).
 * OUT[d] del mittente == IN[opp(d)] del ricevente: i due lati dello stesso
 * canale. Lo stato e' codificato da due contatori mod 2:
 *     pieno / leggibile  :  wp != rp
 *     vuoto / scrivibile :  wp == rp
 */
typedef struct Channel {
    uint32_t data;   /* slot dati (un solo valore, profondita' 1) */
    uint8_t  wp;     /* write pointer (mod 2) */
    uint8_t  rp;     /* read  pointer (mod 2) */
} Channel;

static inline int ch_readable(const Channel *c) { return c->wp != c->rp; }
static inline int ch_writable(const Channel *c) { return c->wp == c->rp; }

/* SETRDY (produttore): se vuoto pubblica e ritorna 1, altrimenti 0. */
static inline int ch_setrdy(Channel *c) {
    if (ch_writable(c)) { c->wp ^= 1; return 1; }
    return 0;
}

/* Lettura consume-on-read: ritorna il dato e, se era pieno, libera lo slot. */
static inline uint32_t ch_read_consume(Channel *c) {
    uint32_t v = c->data;
    if (ch_readable(c)) c->rp ^= 1;
    return v;
}

#endif
