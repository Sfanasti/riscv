/*
    Casi limite del canale a transizione di livello (src/channel.h).
    Tutto lo stato di protocollo sono due contatori mod 2 e i comparatori che
    li confrontano: qui si verifica che basti, cioè che nessuno dei casi
    scomodi richieda un bit in più.
*/

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
    assert(c.data == 42);                  /* il visibile ha catturato alla transizione di wp */

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
        SETRDY pubblica il contenuto CORRENTE del registro di uscita, qualunque
        esso sia: caricarlo è compito della OUT. Senza una OUT davanti
        ripubblica il valore vecchio, e il canale non ha modo di accorgersene.
        È un bug del programma, non del protocollo: accorgersene richiederebbe
        ricordare che una OUT è passata di qui, cioè uno stato in più.
    */
    assert(ch_setrdy(&c) == 1);
    ch_commit(&c);
    assert(ch_isrdy(&c));
    assert(ch_read_c(&c) == 42);           /* il 42 di prima, ripubblicato tale e quale */
    ch_commit(&c);
    assert(ch_iswrt(&c));

    /*
        Il caso per cui tutto questo è stato progettato così.
        Canale pieno, il produttore carica comunque, e il consumatore legge nello
        STESSO ciclo: al ciclo dopo il canale è vuoto e la SETRDY riesce. Deve
        consegnare il valore NUOVO, non un duplicato di quello appena letto.
        È qui che un canale ingenuo sbaglia in silenzio.
    */
    ch_write(&c, 7);
    assert(ch_setrdy(&c) == 1);
    ch_commit(&c);                         /* 7 pubblicato, canale pieno */

    ch_write(&c, 8);                       /* atterra nel registro di uscita, non nel visibile */
    assert(ch_read_c(&c) == 7);            /* il consumatore legge ancora 7: l'8 non lo tocca */
    ch_commit(&c);                         /* consumo committato: vuoto, e il dato è ancora 7 */
    assert(ch_iswrt(&c));
    assert(c.data == 7);                   /* nessuna cattura senza transizione di wp */

    assert(ch_setrdy(&c) == 1);            /* ora si può pubblicare */
    ch_commit(&c);
    assert(ch_isrdy(&c));
    assert(ch_read_c(&c) == 8);            /* ed è l'8, NON un 7 duplicato */
    ch_commit(&c);
    assert(ch_iswrt(&c));

    /*
        Una sola pubblicazione per ciclo. Il caso lo produce solo l'host (un
        risc esegue una istruzione per ciclo, non può fare due OUT+SETRDY):
        la seconda coppia va rifiutata, e la seconda OUT non deve sostituire il
        valore di cui la prima ha già promesso la consegna.
    */
    ch_write(&c, 1);
    assert(ch_setrdy(&c) == 1);
    ch_write(&c, 2);                       /* congelata: la pubblicazione è già decisa */
    assert(ch_setrdy(&c) == 0);
    ch_commit(&c);
    assert(ch_isrdy(&c));                  /* pubblicato una volta, non zero */
    assert(ch_read_c(&c) == 1);            /* ed è il primo valore, non il secondo */
    ch_commit(&c);

    printf("channel: OK\n");
    return 0;
}
