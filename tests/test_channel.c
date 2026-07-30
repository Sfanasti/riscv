#include <assert.h>
#include <stdio.h>
#include "channel.h"

int main(void) {
    Channel c = {0};

    /* vuoto all'inizio */
    assert(ch_iswrt(&c) && !ch_isrdy(&c));

    /* produttore: OUT poi SETRDY -- scrivono solo il "next", non ancora visibili */
    ch_write(&c, 42);
    assert(ch_setrdy(&c) == 1);            /* slot libero -> pubblicazione accettata */
    assert(ch_iswrt(&c) && !ch_isrdy(&c)); /* ma non committata: stato attuale invariato */

    ch_commit(&c);
    assert(ch_isrdy(&c) && !ch_iswrt(&c)); /* ora sì, dopo il commit */

    /* un secondo SETRDY prima che qualcuno legga deve fallire (niente overwrite) */
    assert(ch_setrdy(&c) == 0);

    /* consumatore: IN legge il dato e prenota il consumo, non ancora committato */
    uint32_t v = ch_read_c(&c);
    assert(v == 42);
    assert(ch_isrdy(&c) && !ch_iswrt(&c)); /* stato attuale non ancora aggiornato */

    ch_commit(&c);
    assert(ch_iswrt(&c) && !ch_isrdy(&c)); /* di nuovo vuoto dopo il commit */

    /* leggere a vuoto non deve corrompere lo stato */
    uint32_t stale = ch_read_c(&c);
    (void)stale;
    ch_commit(&c);
    assert(ch_iswrt(&c));

    /*
        SETRDY senza una OUT accettata non deve pubblicare niente:
        altrimenti ripubblica il dato vecchio come se fosse nuovo
    */
    assert(ch_setrdy(&c) == 0);

    /*
        backpressure: OUT su canale pieno viene rifiutata, e la SETRDY che
        segue deve fallire anche se nel frattempo il consumatore ha svuotato
    */
    ch_write(&c, 7);
    assert(ch_setrdy(&c) == 1);
    ch_commit(&c);                         /* 7 pubblicato, canale pieno */
    ch_write(&c, 8);                       /* rifiutata: slot occupato */
    assert(ch_read_c(&c) == 7);            /* il consumatore legge 7 nello stesso ciclo */
    ch_commit(&c);                         /* ora è vuoto... */
    assert(ch_setrdy(&c) == 0);            /* ...ma l'8 non era mai entrato */
    ch_commit(&c);
    assert(!ch_isrdy(&c));                 /* niente 7 duplicato */

    /*
        Una sola pubblicazione per ciclo. Il caso lo produce solo l'host (un
        core esegue una istruzione per ciclo, non può fare due OUT+SETRDY):
        la seconda coppia va rifiutata, non deve sostituire il valore già
        pubblicato né riportare indietro wp_next.
    */
    ch_write(&c, 1);
    assert(ch_setrdy(&c) == 1);
    ch_write(&c, 2);                       /* rifiutata: slot già impegnato */
    assert(ch_setrdy(&c) == 0);
    ch_commit(&c);
    assert(ch_isrdy(&c));                  /* pubblicato una volta, non zero */
    assert(ch_read_c(&c) == 1);            /* ed è il primo valore, non il secondo */
    ch_commit(&c);

    printf("channel: OK\n");
    return 0;
}
