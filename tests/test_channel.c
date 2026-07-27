#include <assert.h>
#include <stdio.h>
#include "channel.h"

int main(void) {
    Channel c = {0};

    /* vuoto all'inizio */
    assert(ch_iswrt(&c) && !ch_isrdy(&c));

    /* produttore: scrive il dato, poi pubblica */
    c.data = 42;
    assert(ch_setrdy(&c) == 1);          /* slot libero -> pubblicazione ok */
    assert(ch_isrdy(&c) && !ch_iswrt(&c));

    /* un secondo SETRDY prima che qualcuno legga deve fallire (niente overwrite) */
    assert(ch_setrdy(&c) == 0);

    /* consumatore: legge e consuma */
    uint32_t v = ch_read_c(&c);
    assert(v == 42);
    assert(ch_iswrt(&c) && !ch_isrdy(&c));  /* di nuovo vuoto */

    /* leggere a vuoto non deve corrompere lo stato (rp non si muove) */
    uint32_t stale = ch_read_c(&c);
    (void)stale;
    assert(ch_iswrt(&c));

    printf("channel: OK\n");
    return 0;
}
