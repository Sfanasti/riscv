.option norvc
.include "macros.s"

# Prodotto C = A x B su griglia RxC: la cella (i,j) accumula C[i][j].
# La riga i di A entra da OVEST e scorre a EST, la colonna j di B entra da
# NORD e scende a SUD. La cella attende entrambi gli operandi, quindi l'host
# non deve sfalsare gli ingressi. L'host alimenta OVEST e NORD e drena EST e
# SUD (tests/test_matmul.c).
#
# a0=riga a1=colonna a2=righe a3=colonne (da grid_init)
# s1 = C[i][j]   s2 = termini rimasti (0 a fine corsa)
#
# PARAMETRI (--defsym):
#   K   lunghezza del prodotto interno, pari ai termini spediti dall'host
#       (default 4)
#
# USO:
#   make run  P=matmul ARCH=rv32im R=2 C=2 N=500 DEFS="--defsym K=4" BORDO=3
#   make test-matmul
# Con BORDO=n costante, s1 = K*n*n in ogni cella.
# Usa mul: richiede ARCH=rv32im.

.ifndef K
.equ K, 4
.endif

.text
.global _start
_start:
    li      s1, 0       # accumulatore = c[i][j]
    li      s2, K       # termini rimasti

# ---- un giro = un termine del prodotto interno ----
iterazione:
1:  ISRDY   t2, OVEST
    beqz    t2, 1b      # niente da ovest: ritenta
    IN      t0, OVEST   # a = A[i][k]

2:  ISRDY   t2, NORD
    beqz    t2, 2b      # niente da nord: ritenta
    IN      t1, NORD    # b = B[k][j]

    mul     t3, t0, t1
    add     s1, s1, t3  # acc += a * b

# ---- inoltro ai vicini a valle ----
3:  OUT     t0, EST     # a prosegue verso destra
    SETRDY  t2, EST
    beqz    t2, 3b      # slot ancora pieno: il vicino non ha consumato

4:  OUT     t1, SUD     # b prosegue verso il basso
    SETRDY  t2, SUD
    beqz    t2, 4b

    addi    s2, s2, -1
    bnez    s2, iterazione
    ecall
