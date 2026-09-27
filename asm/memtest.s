.option norvc

# Verifica di load/store: RAM a byte, little-endian, estensione di segno,
# stop del RISC su accesso fuori dai 16 KB.
# Ogni controllo fallito salta a 'ko'. Atteso: s1 == ATTESO.
#
# USO: make test-mem

.equ ATTESO, 0x11223344

.text
.global _start
_start:
    li      s1, 0
    li      t1, 0x100               # base
    li      t0, ATTESO
    sw      t0, 0(t1)

# 1. lw rilegge ciò che sw ha scritto
    lw      t2, 0(t1)
    bne     t2, t0, ko

# 2. offset in byte, little-endian: 44 33 22 11
    lbu     t2, 0(t1)
    li      t3, 0x44
    bne     t2, t3, ko
    lbu     t2, 3(t1)
    li      t3, 0x11
    bne     t2, t3, ko

# 3. word adiacenti a +4: lo store a +4 non tocca +0
    li      t0, 0x55667788
    sw      t0, 4(t1)
    lw      t2, 0(t1)
    li      t3, ATTESO
    bne     t2, t3, ko
    lw      t2, 4(t1)
    bne     t2, t0, ko

# 4. sb cambia solo il byte 1
    li      t0, 0xAA
    sb      t0, 1(t1)
    lw      t2, 0(t1)
    li      t3, 0x1122AA44
    bne     t2, t3, ko

# 5. lh estende il segno, lhu no
    li      t0, 0xFFFF8001
    sw      t0, 8(t1)
    lh      t2, 8(t1)               # 0x8001 -> -32767
    li      t3, -32767
    bne     t2, t3, ko
    lhu     t2, 8(t1)
    li      t3, 0x8001
    bne     t2, t3, ko

    li      s1, ATTESO

# 6. accesso oltre la RAM (16384): il RISC si ferma qui, altrimenti s1 = 0
    li      t1, 16384
    sw      t0, 0(t1)
    li      s1, 0
    ecall

ko:
    li      s1, 0
    ecall
