.option norvc
.include "macros.s"

# Broadcast su griglia RxC: un valore parte da (0,0) e raggiunge ogni cella.
#   (0,0) --> sorgente: genera VALORE e lo spinge a EST e SUD
#   ogni altra --> aspetta da NORD (o) OVEST, poi lo ripete a EST e SUD
#
# Il dato si propaga come un'onda diagonale: la cella (r,c) lo vede dopo r+c
# hop, da qualunque dei due predecessori gli arrivi per primo.
# Atteso a fine corsa: s1 == VALORE in TUTTE le RxC celle.
#
# Identità precaricata da grid_init: a0=riga a1=colonna a2=righe a3=colonne
# s4 = cicli passati ad aspettare (cresce con r+c: è la forma stessa dell'onda)
#
# USO: make run P=broadcast R=3 C=4 N=400

.ifndef VALORE
.equ VALORE, 4
.endif

.text
.global _start
_start:
    add     t3, a0, a1      #r+c è zero solo per (0,0)
    beqz    t3, source

#----- ogni altra cella: può arrivare da Nord o da Ovest, non si sa quale -----
attesa:
    li      s4, 0
1: ISRDY    t0, NORD
    bnez    t0, 2f
    ISRDY   t0, OVEST
    bnez    t0, 3f
    addi    s4, s4 ,1       # nessuno dei due pronto -> conta e respin
    j       1b
2:  IN      s1, NORD
    j       forwarding
3:  IN      s1, OVEST
    j       forwarding

source:
    li      s1, VALORE

#----- ritrasmissione: due direzioni, due pubblicazioni indipendenti -----
forwarding:
4:  OUT     s1, EST
    SETRDY  t0, EST
    beqz    t0, 4b
5:  OUT     s1, SUD
    SETRDY  t0, SUD
    beqz    t0, 5b
    ecall
    