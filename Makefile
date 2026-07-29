# NESO - simulatore di array RISC-V
#
#   make                                 compila build/stella
#   make asm                             assembla ogni asm/*.s in build/
#   make run  P=<prog> [R= C= N= DEFS=]  esegue sulla griglia, fila dritto
#   make step P=<prog> [R= C= N= DEFS=]  esegue un ciclo per INVIO
#   make test [QUANTI=10]                canale + end-to-end su prodcons.s
#   make clean
#
#   P      nome del .s in asm/, senza estensione     (es. P=prodcons)
#   R C    righe e colonne della griglia             (default 1 2)
#   N      tetto sui cicli: se lo tocca, l'esecuzione e' troncata (default 200)
#   DEFS   simboli passati all'assembler             (es. DEFS="--defsym RITARDO=25")
#
# I parametri dei programmi (RITARDO, QUANTI) stanno nei .s dentro .ifndef,
# quindi hanno un default nel file e si sovrascrivono da qui con DEFS.

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -Isrc
AS      = riscv64-unknown-elf-as
ASFLAGS = -march=rv32i -mabi=ilp32 -Iasm

SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=build/%.o)

ASM_SRC = $(wildcard asm/*.s)
ASM_OBJ = $(ASM_SRC:asm/%.s=build/%.o)

.PHONY: all stella asm test test-channel test-prodcons run step clean

all stella: build/stella

build/stella: $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ $(OBJ)

build/%.o: src/%.c src/*.h | build
	$(CC) $(CFLAGS) -c $< -o $@

asm: $(ASM_OBJ)

build/%.o: asm/%.s | build
	$(AS) $(ASFLAGS) $(DEFS) -o $@ $<

# Riassemblano sempre: cambiando solo DEFS il .s non cambia e make non
# rifarebbe il .o. MODO e' una variabile target-specific: unica ricetta, due
# modalita' (STEP lo legge run_grid in main.c).
R ?= 1
C ?= 2
N ?= 200
run:  MODO =
step: MODO = STEP=1
run step: build/stella
	$(AS) $(ASFLAGS) $(DEFS) -o build/$(P).o asm/$(P).s
	$(MODO) ./build/stella build/$(P).o $(R) $(C) $(N)

test: test-channel test-prodcons

test-channel: tests/test_channel.c src/channel.h | build
	$(CC) $(CFLAGS) -o build/test_channel tests/test_channel.c
	./build/test_channel

# la somma non deve dipendere dalla velocita' relativa dei due nodi:
# stesso programma, stesso risultato atteso, ritardo del consumatore variabile.
# La somma attesa e' derivata da QUANTI, non scritta: make test QUANTI=10
RITARDI = 0 1 4 8 25 60
QUANTI ?= 5
test-prodcons: tests/test_prodcons.c $(SRC) asm/prodcons.s | build
	$(CC) $(CFLAGS) -o build/test_prodcons tests/test_prodcons.c src/core.c src/grid.c src/elf.c
	@for d in $(RITARDI); do \
	  $(AS) $(ASFLAGS) --defsym RITARDO=$$d --defsym QUANTI=$(QUANTI) \
	      -o build/pc_$$d.o asm/prodcons.s || exit 1; \
	  ./build/test_prodcons build/pc_$$d.o $$(( $(QUANTI) * ($(QUANTI) + 1) / 2 )) || exit 1; \
	done

build:
	mkdir -p build

clean:
	rm -rf build
