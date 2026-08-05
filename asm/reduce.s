.option norvc
.include "macros.s"

# Riduzione su griglia RxC: la somma dei contributi di TUTTE le celle finisce
# fuori dal bordo sud-est. Contributo della cella (r,c) = r+c, così il totale
# atteso si ricava dalla forma della griglia invece di essere scritto a mano, e
# una cella cablata al posto sbagliato cambia il risultato.
#
# Due fasi, come in ogni riduzione ad albero su array:
#   1. ogni RIGA si somma da OVEST a EST --> l'ultima colonna ha i totali di riga
#   2. l'ULTIMA COLONNA si somma da NORD a SUD --> (R-1,C-1) ha il totale generale
# Il percorso è lungo (C-1)+(R-1) hop invece dei log di un albero vero, ma qui
# le celle possono parlare solo con i quattro vicini: l'albero non è cablabile.
#
# Nessun caso speciale per l'ultima riga: il suo SUD è un canale di bordo, e a
# drenarlo è l'host esattamente come farebbe la cella sotto se esistesse. È
# lì che si legge il risultato (grid_pop, si veda tests/test_reduce.c).
# Nessun caso speciale nemmeno agli ingressi: la colonna 0 e la riga 0 non
# aspettano un predecessore, lo sanno dalla propria identità cablata.
#
# Identità precaricata da grid_init: a0=riga a1=colonna a2=righe a3=colonne
# s1 = accumulatore (parziale di riga, poi parziale di colonna)
#
# USO: make run P=reduce R=3 C=4 N=400

.text
.global _start
_start:
    add     s1, a0, a1          # contributo di questa cella

# ---- fase 1: riduzione della riga, da OVEST verso EST ----
    beqz    a1, riga_inizio     # la colonna 0 non ha un vicino a ovest
1:  ISRDY   t0, OVEST
    beqz    t0, 1b
    IN      t1, OVEST
    add     s1, s1, t1          # parziale della riga fino a questa colonna

riga_inizio:
    addi    t2, a3, -1
    beq     a1, t2, fase2       # ultima colonna: s1 è il totale della riga
2:  OUT     s1, EST
    SETRDY  t0, EST
    beqz    t0, 2b
    ecall                       # tutte le altre colonne hanno finito qui

# ---- fase 2: riduzione dell'ultima colonna, da NORD verso SUD ----
fase2:
    beqz    a0, colonna_inizio  # la riga 0 non ha un vicino a nord
3:  ISRDY   t0, NORD
    beqz    t0, 3b
    IN      t1, NORD
    add     s1, s1, t1          # parziale: righe 0..r sommate per intero

colonna_inizio:
4:  OUT     s1, SUD
    SETRDY  t0, SUD
    beqz    t0, 4b
    ecall
