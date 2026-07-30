.option norvc
.include "macros.s"

# Prodotto di matrici C = A x B su griglia RxC: la cella (i,j) accumula C[i][j]
# e non si muove.
#
# A scorrere sono i DATI, non i risultati: la riga i di A entra da OVEST e
# attraversa la griglia verso EST, la colonna j di B entra da NORD e scende
# verso SUD. In K giri ogni cella vede passare esattamente gli operandi del
# proprio prodotto interno, e nient'altro.
#
# In un systolic in lockstep l'host deve sfalsare l'ingresso: a[i][k] al ciclo 
# i+k, b[k][j] al ciclo j+k — altrimenti la cella moltiplica la coppia sbagliata 
# e il risultato è errato. Qui la cella si blocca finché non ha ENTRAMBI gli
# operandi, e siccome ogni canale consegna in ordine, il k-esimo valore letto
# da OVEST è A[i][k] e il k-esimo letto da NORD è B[k][j].
#
# Ricevere prima di spedire qui NON va in deadlock, al contrario di jacobi.s.
# Là lo scambio è simmetrico, ogni vicino è insieme sorgente e destinazione;
# qua invece ogni canale ha un verso solo — OVEST e NORD si leggono, EST e SUD si
# scrivono. Le dipendenze vanno tutte verso sud-est, quindi una catena di
# attese non può chiudersi in cerchio ma risale fino al bordo, dove c'è l'host.
#
# Per la stessa ragione l'host DEVE drenare EST e SUD. Il kernel è uniforme e
# nessuna cella sa di stare sul bordo, quindi l'ultima colonna e l'ultima riga
# inoltrano comunque; un OUT che nessuno consuma le inchioda sulla propria
# SETRDY. Lo fa grid_border_drain, si veda tests/test_matmul.c.
#
# PARAMETRI (default qui sotto, si sovrascrive da fuori con --defsym):
# K   lunghezza del prodotto interno --> default 4
#
# K deve coincidere con quanti termini l'host consegna: se il .s ne aspetta più
# di quanti ne arrivano la cella resta appesa e il test muore sul tetto dei
# cicli, se ne aspetta meno si ferma con la somma incompleta. Per questo
# test_matmul.c lo riceve come argomento.
#
# USO:
#   make run  P=matmul ARCH=rv32im R=2 C=2 N=500 DEFS="--defsym K=4"
#   make test-matmul                 K = 1 4 7 su tutte le FORME, con assert
# Unico kernel che usa la M (`mul`), quindi ARCH sale a rv32im solo qui:
# altrove rv32i serve a far rifiutare dall'assemblatore ciò che l'interprete
# non implementa.
#
# Identità precaricata da grid_init: a0=riga a1=colonna a2=righe a3=colonne
# s1 = accumulatore C[i][j]   s2 = termini rimasti
# Li legge test_matmul.c: s1 è il risultato, s2 == 0 dice che i K giri sono
# stati fatti tutti. Niente contatori di spin in s3/s4 come in chain.s:
# li tiene il simulatore da sé (grid_spin).

.ifndef K
.equ K, 4
.endif

.text
.global _start
_start:
    li      s1, 0       # accumulatore = c[i][j]
    li      s2, K       # termini rimasti

# ---- un giro = un termine del prodotto interno ----
# I due ingressi si aspettano a respin, in quest'ordine: la cella non prosegue
# finché non ha la coppia completa. È qui che il ready bit fa il lavoro che
# altrove farebbe una tabella di tempi.
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
# Il respin riparte dalla OUT, non dalla SETRDY: 
# se il canale è pieno la OUT viene rifiutata e non lascia niente in sospeso 
# (il campo `pending` di channel.h resta 0), quindi una SETRDY ripetuta da 
# sola non pubblicherebbe mai e la cella girerebbe a vuoto per sempre.
3:  OUT     t0, EST     # a prosegue verso destra
    SETRDY  t2, EST
    beqz    t2, 3b      # slot ancora pieno: il vicino non ha consumato

4:  OUT     t1, SUD     # b prosegue verso il basso
    SETRDY  t2, SUD
    beqz    t2, 4b

    addi    s2, s2, -1
    bnez    s2, iterazione
    ecall
