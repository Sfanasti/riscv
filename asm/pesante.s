.option norvc
.include "macros.s"

# Kernel sintetico a intensità di calcolo regolabile, per misurare la
# scalabilità OpenMP del simulatore. Comunicazione identica a jacobi.s
# (quattro OUT, poi quattro IN); al posto della media, PESO round di
# xorshift a 32 bit (13, 17, 5) più una costante dispari, che impedisce il
# punto fisso a zero. Aritmetica modulo 2^32, riprodotta bit per bit da
# tests/test_pesante.c.
#
# a0=riga a1=colonna a2=righe a3=colonne (da grid_init)
# s1 = valore corrente   s2 = somma dei vicini   s3 = iterazioni rimaste
# t2 = round rimasti     t3 = temporaneo         t4 = costante additiva
#
# PARAMETRI (--defsym):
#   ITER   iterazioni               (default 8)
#   PESO   round per iterazione     (default 32)
#
# USO: make test-pesante PESO=32
#      make scala

.ifndef ITER
.equ ITER, 8
.endif
.ifndef PESO
.equ PESO, 32
.endif

.text
.global _start
_start:
    add     s1, a0, a1          # seme r+c
    li      s3, ITER
    li      t4, 0x9E3779B9      # costante additiva

iterazione:
# ---- 1. pubblica il valore vecchio a tutti e quattro i vicini ----
1:  OUT     s1, NORD
    SETRDY  t0, NORD
    beqz    t0, 1b
2:  OUT     s1, EST
    SETRDY  t0, EST
    beqz    t0, 2b
3:  OUT     s1, SUD
    SETRDY  t0, SUD
    beqz    t0, 3b
4:  OUT     s1, OVEST
    SETRDY  t0, OVEST
    beqz    t0, 4b

# ---- 2. raccogli i quattro vicini ----
    li      s2, 0
5:  ISRDY   t0, NORD
    beqz    t0, 5b
    IN      t1, NORD
    add     s2, s2, t1
6:  ISRDY   t0, EST
    beqz    t0, 6b
    IN      t1, EST
    add     s2, s2, t1
7:  ISRDY   t0, SUD
    beqz    t0, 7b
    IN      t1, SUD
    add     s2, s2, t1
8:  ISRDY   t0, OVEST
    beqz    t0, 8b
    IN      t1, OVEST
    add     s2, s2, t1

# ---- 3. PESO round di xorshift ----
    mv      s1, s2
    li      t2, PESO
mescola:
    slli    t3, s1, 13
    xor     s1, s1, t3
    srli    t3, s1, 17
    xor     s1, s1, t3
    slli    t3, s1, 5
    xor     s1, s1, t3
    add     s1, s1, t4
    addi    t2, t2, -1
    bnez    t2, mescola

    addi    s3, s3, -1
    bnez    s3, iterazione
    ecall
