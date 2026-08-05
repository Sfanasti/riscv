#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdint.h>

/*
    Canale direzionato, profondità 1, a transizione di livello.
    OUT[d] del mittente == IN[opp(d)] del ricevente: i due lati dello stesso
    canale. Tutto lo stato di protocollo sta in due contatori mod 2 e nei
    comparatori che li confrontano:
        pieno / leggibile  :  wp != rp
        vuoto / scrivibile :  wp == rp
    Il produttore commuta wp per dire "c'è un valore nuovo", il consumatore
    commuta rp per dire "l'ho preso". Non c'è nessun altro bit di stato.

    Contratto: OUT carica il registro di uscita, SETRDY lo pubblica. Il canale
    garantisce da SETRDY in poi: ogni valore pubblicato viene consegnato
    esattamente una volta. Due OUT senza SETRDY in mezzo sono due caricamenti
    dello stesso registro, non due messaggi. Simmetricamente, una SETRDY senza
    OUT davanti ripubblica quello che nel registro c'era già: caricarlo è
    compito della OUT, non del canale.

    Due registri in serie, non un doppio buffer qualunque:
      - data_next è il REGISTRO DI USCITA. Lo carica OUT, ed è privato del
        mittente: il consumatore non lo vede mai, quindi la OUT non ha motivo
        di essere rifiutata e infatti non lo è mai.
      - data è quello VISIBILE, e cattura data_next solo quando wp commuta.
        L'enable è la transizione stessa (si veda ch_commit), quindi un valore
        pubblicato e non ancora letto non può essere sovrascritto da una OUT
        successiva.

    È questa cattura sul fronte che rende superfluo ricordare "ho una OUT in
    sospeso": la OUT può atterrare anche a canale pieno, e sarà la prima SETRDY
    che riesce a consegnarla.
*/

typedef struct Channel {
    uint32_t data;       /* visibile al consumatore: catturato alla transizione di wp */
    uint8_t  wp, rp;     /* write e read  pointer (mod 2) */

    uint32_t data_next;       /* registro di uscita: lo carica OUT */
    uint8_t  wp_next, rp_next;
} Channel;

/* le letture avvengono sempre e solo nello stato attuale */
static inline int ch_isrdy(const Channel *c) {
    return c -> wp != c -> rp;
}
static inline int ch_iswrt(const Channel *c) {
    return c -> wp == c -> rp;
}

/*
    SETRDY (produttore): l'unica delle due che può essere rifiutata, e a
    rifiutarla è il comparatore. Se il canale è vuoto commuta wp e ritorna 1,
    altrimenti 0 e il programma ritenta.
*/
static inline int ch_setrdy(Channel *c) {
    if (ch_iswrt(c) && c -> wp_next == c -> wp) {
        c -> wp_next = c -> wp ^ 1;
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
    OUT: carica il registro di uscita, e non fallisce mai. Il consumatore
    guarda data, non data_next, quindi caricare a canale pieno è innocuo: il
    valore già pubblicato resta intatto e quello nuovo aspetta la SETRDY.
    È il motivo per cui non serve ricordarsi che una OUT è passata di qui.

    L'unica guardia è che il canale accetta al massimo una pubblicazione per
    ciclo: dopo il commit wp_next == wp sempre, quindi wp_next != wp significa
    "pubblicazione di questo ciclo già decisa" e il registro va congelato. Un
    RISC non ci arriva (esegue una istruzione per ciclo), ma l'host che
    alimenta il bordo può chiamare grid_push due volte di fila e senza questa
    guardia la seconda spinta si sostituirebbe al dato di cui la prima ha già
    promesso la consegna.
*/

static inline void ch_write(Channel *c, uint32_t v) {
    if (c -> wp_next == c -> wp) {
        c -> data_next = v;
    }
}

/*
    commit: i due contatori passano sempre, il dato solo sulla TRANSIZIONE di
    wp. Quel confronto è il segnale di pubblicazione, cioè l'enable del
    registro visibile: senza, una OUT a canale pieno cancellerebbe un valore
    che il consumatore non ha ancora letto.
*/
static inline void ch_commit(Channel *c) {
    if (c->wp_next != c->wp) {
        c->data = c->data_next;
    }
    c->wp = c->wp_next;
    c->rp = c->rp_next;
}

#endif
