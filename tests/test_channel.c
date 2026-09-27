/* casi limite del canale (src/channel.h): bastano i due contatori mod 2 */

#include <assert.h>
#include <stdio.h>
#include "channel.h"

int main(void) {
    Channel c = {0};

    /* vuoto all'inizio */
    assert(ch_iswrt(&c) && !ch_isrdy(&c));

    /* OUT e SETRDY scrivono solo i campi next */
    ch_write(&c, 42);
    assert(ch_setrdy(&c) == 1);            /* slot libero */
    assert(ch_iswrt(&c) && !ch_isrdy(&c)); /* non ancora visibile */

    ch_commit(&c);
    assert(ch_isrdy(&c) && !ch_iswrt(&c)); /* visibile dopo il commit */
    assert(c.data == 42);

    /* seconda SETRDY prima della lettura: rifiutata */
    assert(ch_setrdy(&c) == 0);

    /* IN legge il dato; il consumo è visibile al commit */
    uint32_t v = ch_read_c(&c);
    assert(v == 42);
    assert(ch_isrdy(&c) && !ch_iswrt(&c));

    ch_commit(&c);
    assert(ch_iswrt(&c) && !ch_isrdy(&c)); /* di nuovo vuoto */

    /* IN a vuoto non altera lo stato */
    uint32_t stale = ch_read_c(&c);
    (void)stale;
    ch_commit(&c);
    assert(ch_iswrt(&c));

    /* SETRDY senza OUT ripubblica il contenuto del registro di uscita */
    assert(ch_setrdy(&c) == 1);
    ch_commit(&c);
    assert(ch_isrdy(&c));
    assert(ch_read_c(&c) == 42);           /* il 42 di prima */
    ch_commit(&c);
    assert(ch_iswrt(&c));

    /*
        OUT a canale pieno e IN nello stesso ciclo: la SETRDY successiva
        consegna il valore nuovo, non un duplicato
    */
    ch_write(&c, 7);
    assert(ch_setrdy(&c) == 1);
    ch_commit(&c);                         /* canale pieno */

    ch_write(&c, 8);                       /* nel registro di uscita */
    assert(ch_read_c(&c) == 7);
    ch_commit(&c);
    assert(ch_iswrt(&c));
    assert(c.data == 7);                   /* data cambia solo con wp */

    assert(ch_setrdy(&c) == 1);
    ch_commit(&c);
    assert(ch_isrdy(&c));
    assert(ch_read_c(&c) == 8);            /* l'8, non un 7 duplicato */
    ch_commit(&c);
    assert(ch_iswrt(&c));

    /*
        due OUT+SETRDY nello stesso ciclo (solo dall'host): la seconda è
        rifiutata e non sostituisce il valore
    */
    ch_write(&c, 1);
    assert(ch_setrdy(&c) == 1);
    ch_write(&c, 2);
    assert(ch_setrdy(&c) == 0);
    ch_commit(&c);
    assert(ch_isrdy(&c));
    assert(ch_read_c(&c) == 1);            /* il primo valore */
    ch_commit(&c);

    printf("channel: OK\n");
    return 0;
}
