.option norvc

# Verifica di lw/sw: la RAM del core e' byte-indirizzata come la vede il
# caricatore ELF, non indicizzata a parole, e un accesso fuori dai 16 KB ferma
# il core invece di sconfinare nella cella successiva dell'array flat.
#
# Un solo assert copre tutti i passi: ogni controllo che fallisce salta a 'ko',
# che azzera s1, e il test e' "s1 == ATTESO alla fine".
#
# USO: make test-mem

.equ ATTESO, 0x11223344

.text
.global _start
_start:
    li      s1, 0                   # resta 0 se qualcosa va storto
    li      t1, 0x100               # base: byte 256, ben dentro i 16 KB
    li      t0, ATTESO
    sw      t0, 0(t1)

# 1. lw rilegge esattamente cio' che sw ha scritto
    lw      t2, 0(t1)
    bne     t2, t0, ko

# 2. gli offset sono byte, non parole: i 4 byte del word stanno a 0..3,
#    little-endian, quindi 44 33 22 11 in quest'ordine
    lbu     t2, 0(t1)
    li      t3, 0x44
    bne     t2, t3, ko
    lbu     t2, 3(t1)
    li      t3, 0x11
    bne     t2, t3, ko

# 3. e i word adiacenti distano 4 byte, non 1: uno store a +4 non tocca +0
    li      t0, 0x55667788
    sw      t0, 4(t1)
    lw      t2, 0(t1)
    li      t3, ATTESO
    bne     t2, t3, ko
    lw      t2, 4(t1)
    bne     t2, t0, ko

# 4. store stretto dentro un word: sb cambia solo il byte 1
    li      t0, 0xAA
    sb      t0, 1(t1)
    lw      t2, 0(t1)
    li      t3, 0x1122AA44
    bne     t2, t3, ko

# 5. lh/lb estendono il segno, lhu/lbu no
    li      t0, 0xFFFF8001
    sw      t0, 8(t1)
    lh      t2, 8(t1)               # 0x8001 -> -32767
    li      t3, -32767
    bne     t2, t3, ko
    lhu     t2, 8(t1)               # stesso mezzo word, senza segno
    li      t3, 0x8001
    bne     t2, t3, ko

    li      s1, ATTESO              # tutti i controlli passati

# 6. l'ultimo accesso e' fuori RAM (16384 = primo byte oltre la fine): il core
#    deve fermarsi QUI, e s1 resta buono. Se la guardia manca, l'esecuzione
#    prosegue e le due istruzioni dopo azzerano s1, facendo fallire il test.
    li      t1, 16384
    sw      t0, 0(t1)
    li      s1, 0
    ecall

ko:
    li      s1, 0
    ecall
