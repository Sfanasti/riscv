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

.PHONY: all stella asm test test-channel test-prodcons test-chain run step clean

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

test: test-channel test-prodcons test-chain

test-channel: tests/test_channel.c src/channel.h | build
	$(CC) $(CFLAGS) -o build/test_channel tests/test_channel.c
	./build/test_channel

# Il risultato non deve dipendere ne' dalla velocita' relativa dei nodi
# (RITARDI) ne' dalla lunghezza della catena (COLONNE). La somma attesa e'
# derivata da QUANTI, non scritta a mano: make test QUANTI=10
RITARDI = 0 1 4 8 25 60
COLONNE = 2 3 5 12
QUANTI ?= 5
ATTESA  = $$(( $(QUANTI) * ($(QUANTI) + 1) / 2 ))

# un solo binario di test per entrambi i programmi: prodcons.s e' il caso C=2
build/test_catena: tests/test_catena.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_catena.c src/core.c src/grid.c src/elf.c

test-prodcons: build/test_catena asm/prodcons.s
	@for d in $(RITARDI); do \
	  $(AS) $(ASFLAGS) --defsym RITARDO=$$d --defsym QUANTI=$(QUANTI) \
	      -o build/pc_$$d.o asm/prodcons.s || exit 1; \
	  ./build/test_catena build/pc_$$d.o 2 $(ATTESA) || exit 1; \
	done

test-chain: build/test_catena asm/chain.s
	@for d in $(RITARDI); do \
	  $(AS) $(ASFLAGS) --defsym RITARDO=$$d --defsym QUANTI=$(QUANTI) \
	      -o build/ch_$$d.o asm/chain.s || exit 1; \
	  for c in $(COLONNE); do \
	    ./build/test_catena build/ch_$$d.o $$c $(ATTESA) || exit 1; \
	  done; \
	done

build:
	mkdir -p build

clean:
	rm -rf build
