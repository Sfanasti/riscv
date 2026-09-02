.option norvc
.include "macros.s"

# Kernel sintetico a intensità di calcolo regolabile.
#
# Non calcola niente di utile, e non finge di farlo: serve a UNA cosa sola,
# spostare il rapporto fra calcolo e comunicazione per misurare dove cade il
# punto di pareggio della parallelizzazione OpenMP del simulatore.
# Ogni altro kernel ha quel rapporto fissato dall'algoritmo; qui è un parametro.
#
# La comunicazione è identica a jacobi.s: quattro OUT verso i vicini, poi
# quattro IN da loro. Ne eredita per intero l'argomento di assenza di
# deadlock su canali di profondità 1, che vale parola per parola: se tutte le
# celle fossero bloccate, quella all'iterazione più bassa non potrebbe essere
# in spedizione (aspetterebbe qualcuno a un'iterazione ancora inferiore),
# quindi sarebbe in ricezione — ma una cella in ricezione ha già pubblicato
# tutti e quattro i suoi valori, quindi ciò che aspetta c'è già.
# Il mescolamento non blocca mai, dunque non entra nell'argomento.
#
# Cosa cambia rispetto a jacobi.s è solo il terzo passo: invece della media
# dei quattro vicini (sei istruzioni), PESO round di mescolamento sulla loro
# somma, nove istruzioni l'uno. A PESO=32 sono 288 istruzioni di calcolo
# contro le 16 di canale, contro le 6 contro 16 di Jacobi.
#
# Il mescolamento è lo xorshift a 32 bit di Marsaglia (13, 17, 5) con una
# costante additiva. Tre proprietà, tutte necessarie qui:
#   - sta in RV32I: solo slli, srli, xor, add. Nessun bisogno della M.
#   - è deterministico bit per bit, quindi il riferimento in C di
#     tests/test_pesante.c non modella il kernel, rifà le stesse operazioni
#     sugli stessi uint32_t. srli è lo shift LOGICO, da cui uint32_t in C.
#   - la costante additiva dispari impedisce che lo zero resti zero. Senza,
#     una griglia inizializzata a zero con contorno zero non si muoverebbe
#     mai: è lo stesso punto fisso spurio che jacobi.s evita con
#     l'arrotondamento, in altra forma.
#
# L'overflow non è un problema ma il meccanismo stesso: l'aritmetica è
# modulo 2^32 per costruzione, e il riferimento in C fa altrettanto.
#
# Identità precaricata da grid_init: a0=riga a1=colonna a2=righe a3=colonne
# s1 = valore corrente   s2 = somma dei vicini   s3 = iterazioni rimaste
# t2 = round rimasti     t3 = temporaneo         t4 = costante additiva
#
# PARAMETRI (default qui sotto, si sovrascrivono da fuori con --defsym):
#   ITER   quante iterazioni complete --> default 8
#   PESO   round di mescolamento per iterazione --> default 32
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
    add     s1, a0, a1          # seme r+c: aritmetica sulla posizione, non
                                # controllo di flusso — un solo percorso di
                                # esecuzione per tutte le celle
    li      s3, ITER
    li      t4, 0x9E3779B9      # costante additiva (lui + addi, RV32I)

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
#
# L'ordine di accumulo non conta: la somma è commutativa anche modulo 2^32,
# quindi il riferimento in C può sommare i quattro nell'ordine che preferisce.
#
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

# ---- 3. il carico: PESO round di mescolamento, nove istruzioni l'uno ----
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
