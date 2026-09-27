.option norvc
.include "macros.s"

# Jacobi a 5 punti su griglia RxC: ITER volte u(r,c) <- media dei 4 vicini.
# Kernel uniforme: il contorno arriva dall'host (BORDO=n), che drena anche
# le uscite di perimetro.
# Tutte le OUT precedono tutte le IN: evita il deadlock su canali di
# profondità 1. Si spedisce il valore vecchio: Jacobi, non Gauss-Seidel.
#
# a0=riga a1=colonna a2=righe a3=colonne (da grid_init)
# s1 = u   s2 = somma dei vicini   s3 = iterazioni rimaste
#
# PARAMETRI (--defsym):
#   ITER   iterazioni              (default 32)
#   SEME   0: u=0, 1: u=r+c        (default 0)
#
# USO: make run P=jacobi R=4 C=4 N=4000 BORDO=64
#      make test-jacobi ITER=64 VALORE_BORDO=64

.ifndef ITER
.equ ITER, 32
.endif
.ifndef SEME
.equ SEME, 0
.endif

.text
.global _start
_start:
.if SEME == 0
    li      s1, 0               # interno freddo
.else
    add     s1, a0, a1          # campo iniziale r+c
.endif
    li      s3, ITER

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

# ---- 3. media arrotondata: (somma + 2) >> 2 ----
# La sola troncatura ha un punto fisso spurio sotto la soluzione.
    addi    s2, s2, 2
    srai    s1, s2, 2

    addi    s3, s3, -1
    bnez    s3, iterazione
    ecall
