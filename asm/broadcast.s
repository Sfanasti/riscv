.option norvc
.include "macros.s"

# Broadcast su griglia RxC: VALORE parte da (0,0) e raggiunge ogni cella.
#   (0,0)      sorgente: spinge VALORE a EST e SUD
#   le altre   attendono da NORD o OVEST, poi ripetono a EST e SUD
# Atteso: s1 == VALORE in tutte le celle.
#
# a0=riga a1=colonna a2=righe a3=colonne (da grid_init)
# s4 = cicli di attesa (cresce con r+c)
#
# USO: make run P=broadcast R=3 C=4 N=400

.ifndef VALORE
.equ VALORE, 4
.endif

.text
.global _start
_start:
    add     t3, a0, a1      # r+c è zero solo per (0,0)
    beqz    t3, source

#----- altre celle: attendono da NORD o OVEST -----
attesa:
    li      s4, 0
1: ISRDY    t0, NORD
    bnez    t0, 2f
    ISRDY   t0, OVEST
    bnez    t0, 3f
    addi    s4, s4 ,1       # nessuno dei due pronto -> conta e ritenta
    j       1b
2:  IN      s1, NORD
    j       forwarding
3:  IN      s1, OVEST
    j       forwarding

source:
    li      s1, VALORE

#----- inoltro a EST e SUD -----
forwarding:
4:  OUT     s1, EST
    SETRDY  t0, EST
    beqz    t0, 4b
5:  OUT     s1, SUD
    SETRDY  t0, SUD
    beqz    t0, 5b
    ecall
