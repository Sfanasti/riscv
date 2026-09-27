.option norvc
.include "macros.s"

# Produttore/consumatore su griglia 1x2.
#   colonna 0: manda 1..Q a EST
#   colonna 1: legge da OVEST e accumula in s1
# Atteso: s1 della colonna 1 == Q*(Q+1)/2 per ogni RITARDO.
#
# a0=riga a1=colonna a2=righe a3=colonne (da grid_init)
# s3 = SETRDY rifiutate (produttore)   s4 = ISRDY a vuoto (consumatore)
#
# PARAMETRI (--defsym):
#   Q         valori spediti                              (default 5)
#   RITARDO   cicli di attesa del consumatore per lettura (default 0)
#
# USO:
#   make run  P=prodcons R=1 C=2 N=2000 DEFS="--defsym RITARDO=25"
#   make step P=prodcons R=1 C=2 N=40
#   make test-prodcons [Q=10]
#   make test-nobp     [Q=10]
# Se l'uscita dice "limite cicli raggiunto", N è troppo basso.

.ifndef RITARDO
.equ RITARDO, 0
.endif
.ifndef Q
.equ Q, 5
.endif

.text
.global _start
_start:
    mv     s0, a1          # s0 = colonna
    beqz   s0, producer    # colonna 0 -> produce
    j      consumer

producer:
    li     s1, 1           # valore corrente
    li     s2, Q           # quanti ne restano
    li     s3, 0           # SETRDY rifiutate
1:  OUT    s1, EST
    SETRDY t0, EST
    bnez   t0, 5f
    addi   s3, s3, 1       # canale ancora pieno -> conta e ritenta OUT+SETRDY
    j      1b
5:  addi   s1, s1, 1
    addi   s2, s2, -1
    bnez   s2, 1b
    ecall

consumer:
    li     s1, 0           # somma
    li     s2, Q
    li     s4, 0           # ISRDY a vuoto
2:
.if RITARDO > 0
    li     t2, RITARDO     # consumatore lento
3:  addi   t2, t2, -1
    bnez   t2, 3b
.endif
4:  ISRDY  t0, OVEST
    bnez   t0, 6f
    addi   s4, s4, 1       # niente da leggere -> conta e ritenta
    j      4b
6:  IN     t1, OVEST
    add    s1, s1, t1
    addi   s2, s2, -1
    bnez   s2, 2b
    ecall
