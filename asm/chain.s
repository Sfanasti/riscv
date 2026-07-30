.option norvc
.include "macros.s"

# "Generalizzazione" di prodcons.s
# Catena su griglia 1xC: source -> forwarding ... forwarding -> sink.
# Il ruolo viene dalla POSIZIONE, non da un id:
#   colonna 0 --> source: manda 1..QUANTI a EST
#   colonna cols-1 --> sink:legge da OVEST e accumula in s1
#   in mezzo --> forwarding: legge da OVEST e ripubblica a EST
#
# Atteso: s1 del sink == QUANTI*(QUANTI+1)/2 per QUALSIASI numero di colonne
# e QUALSIASI valore di RITARDO. La catena cambia la latenza ma non deve
# cambiare il risultato.
#
# Con C=2 non ci sono nodi di mezzo e il programma degenera in prodcons.s.
#
# Registri di identità precaricati da grid_init:
#   a0 = riga   a1 = colonna   a2 = righe totali   a3 = colonne totali
#
# Contatori per il debug, nella stampa finale di ogni cella:
#   s3 = SETRDY rifiutate (source e forwarding: quanto il nodo è stato frenato)
#   s4 = ISRDY a vuoto    (forwarding e sink: quanto è rimasto a digiuno)
# Su una catena lunga si legge dove sta il collo di bottiglia: la cella con s3
# alto e s4 basso è quella che aspetta il vicino a valle.
#
# PARAMETRI (default qui sotto, si sovrascrivono da fuori con --defsym):
#   QUANTI    quanti valori attraversano la catena --> default 5
#   RITARDO   cicli sprecati dal sink prima di ogni lettura --> default 0
#
# USO:
#   make run  P=chain R=1 C=6 N=4000 DEFS="--defsym RITARDO=10"
#   make step P=chain R=1 C=4 N=60
#   make test-chain [QUANTI=10]     sweep 6 RITARDI x 4 lunghezze, assert sulla somma
#
# Come in prodcons.s, RITARDO non si passa da qui: lo fa variare il Makefile
# (RITARDI = 0 1 4 8 25 60), e COLONNE = 2 3 5 12 gli si moltiplica sopra, perché
# la somma non deve dipendere né dalla velocità dei nodi né dalla lunghezza.
#
# N va dimensionato su C e RITARDO insieme: ogni hop aggiunge latenza. Se la
# stampa finale dice "limite cicli raggiunto" i registri non sono un risultato.

.ifndef RITARDO
.equ RITARDO, 0
.endif
.ifndef QUANTI
.equ QUANTI, 5
.endif

.text
.global _start
_start:
    mv     s0, a1          # s0 = la mia colonna
    addi   s5, a3, -1      # s5 = indice dell'ultima colonna
    beqz   s0, source
    beq    s0, s5, sink
    j      forwarding

# ---- colonna 0: genera 1..QUANTI verso EST ----
source:
    li     s1, 1           # valore corrente
    li     s2, QUANTI      # quanti ne restano
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

# ---- colonne intermedie: OVEST -> EST, QUANTI volte ----
# Sa quando fermarsi perché QUANTI è una costante di build nota a tutti:
# non serve un valore sentinella nel flusso dati.
forwarding:
    li     s2, QUANTI
    li     s3, 0
    li     s4, 0
1:  ISRDY  t0, OVEST
    bnez   t0, 2f
    addi   s4, s4, 1       # niente in arrivo -> conta e respin
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
    li     s2, QUANTI
    li     s4, 0
1:
.if RITARDO > 0
    li     t2, RITARDO     # sink lento: mette in backpressure tutta la catena
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
