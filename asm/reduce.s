.option norvc
.include "macros.s"

# Riduzione su griglia RxC: somma dei contributi r+c di tutte le celle.
#   fase 1: ogni riga somma da OVEST a EST
#   fase 2: l'ultima colonna somma da NORD a SUD
# Il totale esce dal SUD di (R-1,C-1), dove lo legge l'host
# (tests/test_reduce.c).
#
# a0=riga a1=colonna a2=righe a3=colonne (da grid_init)
# s1 = accumulatore
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
