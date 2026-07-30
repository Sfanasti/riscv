# Istruzioni custom dell'array NESO - opcode custom-0 = 0x0B (spazio libero RISC_V)
# Formato I-type:   .insn i opcode, funct3, rd, rs1, imm
#   | imm[11:0] | rs1 | funct3 | rd | opcode |
#   |  31..20   |19.15| 14..12 |11.7|  6..0  |
# La direzione sta nei 2 bit bassi dell'immediato
.equ NORD,  0
.equ EST,   1
.equ SUD,   2
.equ OVEST, 3

# --- percorso dati  ---
.macro IN rd, dir              # rd <- IN[dir]            (funct3=0)
    .insn i 0x0B, 0x0, \rd, x0, \dir
.endm
.macro OUT rs, dir             # OUT[dir] <- rs           (funct3=1)
    .insn i 0x0B, 0x1, x0, \rs, \dir
.endm

# --- ready-bit (le due nuove, task 1) ---
.macro ISRDY rd, dir           # rd <- (IN[dir] leggibile? 1 : 0)     (funct3=2)
    .insn i 0x0B, 0x2, \rd, x0, \dir
.endm
.macro SETRDY rd, dir          # pubblica OUT[dir]; rd <- esito (1/0)  (funct3=3)
    .insn i 0x0B, 0x3, \rd, x0, \dir
.endm
