#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdint.h>

typedef struct Channel {
    uint32_t data;       /* visibile al consumatore */
    uint8_t  wp, rp;     /* contatori mod 2 */

    uint32_t data_next;       /* registro di uscita */
    uint8_t  wp_next, rp_next;
} Channel;

static inline int ch_isrdy(const Channel *c) {
    return c -> wp != c -> rp;
}
static inline int ch_iswrt(const Channel *c) {
    return c -> wp == c -> rp;
}

/* SETRDY */
static inline int ch_setrdy(Channel *c) {
    if (ch_iswrt(c) && c -> wp_next == c -> wp) {
        c -> wp_next = c -> wp ^ 1;
        return 1;
    }
    return 0;
}

/* IN */
static inline uint32_t ch_read_c(Channel *c) {
    uint32_t v = c -> data;
    if (ch_isrdy(c)) {
        c -> rp_next = c -> rp ^ 1;
    }
    return v;
}

/* OUT */
static inline void ch_write(Channel *c, uint32_t v) {
    if (c -> wp_next == c -> wp) {
        c -> data_next = v;
    }
}

/* fine ciclo */
static inline void ch_commit(Channel *c) {
    if (c->wp_next != c->wp) {
        c->data = c->data_next;
    }
    c->wp = c->wp_next;
    c->rp = c->rp_next;
}

#endif
