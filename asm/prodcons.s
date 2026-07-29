.option norvc
.include "macros.s"

# Produttore/consumatore su griglia 1x2 - il caso minimo del protocollo.
# Per una catena piu' lunga vedi chain.s, che generalizza questo con i nodi
# di inoltro in mezzo; qui restano solo i due ruoli, per leggerli puliti.
#   colonna 0 (a1=0): manda 1..QUANTI a EST
#   colonna 1 (a1=1): legge da OVEST e accumula in s1
#
# Registri di identita' precaricati da grid_init:
#   a0 = riga   a1 = colonna   a2 = righe totali   a3 = colonne totali
# Atteso: s1 del nodo 1 == QUANTI*(QUANTI+1)/2, per QUALSIASI valore di RITARDO.
#
# Contatori per il debug, leggibili nella stampa finale:
#   nodo 0  s3 = SETRDY rifiutate  (quanto il produttore ha aspettato: backpressure)
#   nodo 1  s4 = ISRDY a vuoto     (quanto il consumatore e' rimasto a digiuno)
# Dicono da che parte sta il collo di bottiglia. A RITARDO basso aspettano
# entrambi (misurato: 2 e 3); alzando RITARDO s4 va a 0 e s3 cresce.
#
# PARAMETRI (default qui sotto, si sovrascrivono da fuori con --defsym):
#   QUANTI    quanti valori spedisce il produttore                    default 5
#   RITARDO   cicli sprecati dal consumatore prima di ogni lettura    default 0
#
# USO:
#   make run  P=prodcons R=1 C=2 N=2000 DEFS="--defsym RITARDO=25"
#   make step P=prodcons R=1 C=2 N=40          un ciclo per INVIO
#   make test QUANTI=10                        sweep di RITARDO, con assert
#
# N va dimensionato sul RITARDO: se la stampa finale dice "limite cicli
# raggiunto" l'esecuzione e' stata troncata e i registri non sono un risultato.
# Ordine di grandezza: RITARDO=25 con QUANTI=5 chiude in ~292 cicli.

.ifndef RITARDO
.equ RITARDO, 0            # cicli sprecati dal consumatore prima di ogni lettura
.endif
.ifndef QUANTI
.equ QUANTI, 5             # quanti valori spedisce il produttore
.endif

.text
.global _start
_start:
    mv     s0, a1          # s0 = la mia colonna: a1 lo puo' sporcare chiunque
    beqz   s0, produttore  # colonna 0 -> produce
    j      consumatore

produttore:
    li     s1, 1           # valore corrente
    li     s2, QUANTI      # quanti ne restano
    li     s3, 0           # RITENTATIVI: quante SETRDY il canale ha rifiutato
1:  OUT    s1, EST
    SETRDY t0, EST
    bnez   t0, 5f
    addi   s3, s3, 1       # canale ancora pieno -> conta e ritenta OUT+SETRDY
    j      1b
5:  addi   s1, s1, 1
    addi   s2, s2, -1
    bnez   s2, 1b
    ecall

consumatore:
    li     s1, 0           # somma
    li     s2, QUANTI
    li     s4, 0           # ATTESE: quanti cicli ha trovato il canale vuoto
2:
.if RITARDO > 0
    li     t2, RITARDO     # consumatore lento: mette il produttore in backpressure
3:  addi   t2, t2, -1
    bnez   t2, 3b
.endif
4:  ISRDY  t0, OVEST
    bnez   t0, 6f
    addi   s4, s4, 1       # niente da leggere -> conta e rispin
    j      4b
6:  IN     t1, OVEST
    add    s1, s1, t1
    addi   s2, s2, -1
    bnez   s2, 2b
    ecall
