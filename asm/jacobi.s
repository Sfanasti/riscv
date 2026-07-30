.option norvc
.include "macros.s"

# Stencil kernel: algoritmo in cui il valore di ogni elemento di una griglia viene 
# aggiornato usando i valori dei suoi vicini.
#
# Jacobi a 5 punti su griglia RxC: u(r,c) -> media dei quattro vicini, ITER volte.
#
# Primo kernel completamente uniforme: nessun salto condizionato sulla
# posizione, nemmeno ai margini. Le celle di perimetro leggono la condizione al
# contorno dal canale di bordo come leggerebbero un vicino (l'host la alimenta
# con BORDO=n) e pubblicano verso l'esterno come pubblicherebbero verso un
# vicino (l'host drena). 
# Da notare il contrasto con reduce.s, che deve chiedersi "sono la colonna 0?": la sua asimmetria è 
# dell'algoritmo, non del bordo, e nessun supporto dell'host la toglierà mai.
#
# I quattro OUT vanno TUTTI prima dei quattro IN, e non è una preferenza
# stilistica: è la condizione per cui il kernel non va in deadlock su canali
# di profondità 1. Se tutte le celle fossero bloccate, quella all'iterazione
# più bassa non potrebbe essere in spedizione (aspetterebbe qualcuno a
# un'iterazione ancora inferiore), ovvero sarebbe in ricezione — ma una cella
# in ricezione ha per costruzione già pubblicato tutti e quattro i suoi
# valori, quindi ciò che aspetta c'è già. Contraddizione.
# Ricevere prima di spedire va in deadlock al primo giro; alternare per
# direzione si sblocca solo per come sono disposti i bordi.
#
# È appunto Jacobi, non Gauss-Seidel: si spedisce il valore VECCHIO prima di
# calcolare il nuovo: tutti leggono l'iterazione k per produrre la k+1.
#
# Identità precaricata da grid_init: a0=riga a1=colonna a2=righe a3=colonne
# s1 = u (valore corrente)   s2 = somma dei vicini   s3 = iterazioni rimaste
#
# PARAMETRI (default qui sotto, si sovrascrivono da fuori con --defsym):
#   ITER   quante iterazioni di Jacobi --> default 32
#   SEME   0 = interno freddo (u=0), 1 = campo iniziale u=r+c --> default 0
#
# La tolleranza sull'errore non vive qui: il riferimento in C di
# tests/test_jacobi.c è esatto bit per bit, quindi max|u_k - u_k-1| calcolato
# lì è la convergenza di questo kernel.
#
# USO: make run P=jacobi R=4 C=4 N=4000 BORDO=64
#      make test-jacobi ITER=64 BORDO=64

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
    li      s1, 0               # interno freddo: il calore entra solo dal bordo
.else
    add     s1, a0, a1          # campo iniziale r+c: aritmetica sulla posizione,
                                # non controllo di flusso — resta un solo
                                # percorso di esecuzione per tutte le celle
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

# ---- 3. media dei quattro, arrotondata al più vicino ----
#
# srai secco tronca, e la troncatura è un bias sistematico verso il
# basso che crea un PUNTO FISSO SPURIO: quando i quattro vicini valgono B-1
# il risultato resta B-1, quindi il campo si ferma sotto la soluzione e non ci arriva
# più per nessun numero di iterazioni (misurato su 4x4 BORDO=64: stallo a 60..62 
# anche a ITER=64). 
# Con l'arrotondamento converge esatto a 64.
# La somma di quattro valori sta larga fino a BORDO ~ 2^29, evitando dunque overflow.
#
    addi    s2, s2, 2
    srai    s1, s2, 2

    addi    s3, s3, -1
    bnez    s3, iterazione
    ecall
