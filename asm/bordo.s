.option norvc
.include "macros.s"

# Eco verticale: ogni cella legge da NORD e ripete a SUD.
# La prima riga è alimentata dall'host, l'ultima è drenata dall'host.
# Atteso: s1 == valore spinto in tutte le celle.
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
