.option norvc
.include "macros.s"

# Echo verticale: ogni cella aspetta un valore da NORD e lo ripete a SUD.
# Su una griglia RxC le colonne sono indipendenti e il dato attraversa tutta
# la griglia dall'alto in basso.
#
# Serve a dimostrare l'I/O di bordo dell'host, perché i due capi della catena
# cadono fuori dalla griglia:
#   la prima riga  aspetta su NORD un vicino che non esiste -> lo alimenta l'host
#   l'ultima riga  pubblica su SUD verso nessuno -> lo drena l'host
# Senza il drenaggio l'ultima riga resterebbe bloccata sulla propria SETRDY:
# è il motivo per cui grid_pop esiste e non basta "leggere" il canale.
#
# Atteso a fine corsa: s1 == valore spinto, in TUTTE le RxC celle.
#
# USO: make test-bordo

.text
.global _start
_start:
1:  ISRDY   t0, NORD
    beqz    t0, 1b
    IN      s1, NORD

2:  OUT     s1, SUD
    SETRDY  t0, SUD
    beqz    t0, 2b
    ecall
