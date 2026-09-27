.option norvc
.include "macros.s"

# Catena su griglia 1xC, generalizza prodcons.s:
#   colonna 0     source: manda 1..Q a EST
#   in mezzo      forwarding: da OVEST a EST
#   colonna C-1   sink: legge da OVEST e accumula in s1
# Atteso: s1 del sink == Q*(Q+1)/2 per ogni C e ogni RITARDO.
#
# a0=riga a1=colonna a2=righe a3=colonne (da grid_init)
# s3 = SETRDY rifiutate (source, forwarding)
# s4 = ISRDY a vuoto (forwarding, sink)
#
# PARAMETRI (--defsym):
#   Q         valori spediti                        (default 5)
#   RITARDO   cicli di attesa del sink per lettura  (default 0)
#
# USO:
#   make run  P=chain R=1 C=6 N=4000 DEFS="--defsym RITARDO=10"
#   make step P=chain R=1 C=4 N=60
#   make test-chain [Q=10]
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
    addi   s5, a3, -1      # s5 = indice dell'ultima colonna
    beqz   s0, source
    beq    s0, s5, sink
    j      forwarding

# ---- colonna 0: genera 1..Q verso EST ----
source:
    li     s1, 1           # valore corrente
    li     s2, Q           # quanti ne restano
    li     s3, 0           # SETRDY rifiutate
1:  OUT    s1, EST
    SETRDY t0, EST
    bnez   t0, 2f
    addi   s3, s3, 1       # canale a valle ancora pieno -> conta e ritenta
    j      1b
2:  addi   s1, s1, 1
    addi   s2, s2, -1
    bnez   s2, 1b
    ecall

# ---- colonne intermedie: OVEST -> EST, Q volte ----
forwarding:
    li     s2, Q
    li     s3, 0
    li     s4, 0
1:  ISRDY  t0, OVEST
    bnez   t0, 2f
    addi   s4, s4, 1       # niente in arrivo -> conta e ritenta
    j      1b
2:  IN     t1, OVEST
3:  OUT    t1, EST
    SETRDY t0, EST
    bnez   t0, 4f
    addi   s3, s3, 1       # a valle ancora pieno -> conta e ritenta OUT+SETRDY
    j      3b
4:  addi   s2, s2, -1
    bnez   s2, 1b
    ecall

# ---- ultima colonna: accumula in s1 ----
sink:
    li     s1, 0           # somma
    li     s2, Q
    li     s4, 0
1:
.if RITARDO > 0
    li     t2, RITARDO     # sink lento
2:  addi   t2, t2, -1
    bnez   t2, 2b
.endif
3:  ISRDY  t0, OVEST
    bnez   t0, 4f
    addi   s4, s4, 1
    j      3b
4:  IN     t1, OVEST
    add    s1, s1, t1
    addi   s2, s2, -1
    bnez   s2, 1b
    ecall
