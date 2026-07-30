# NESO - simulatore di array RISC_V
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
# I/O di bordo (l'host fa il vicino che le celle di perimetro non hanno):
#   BORDO=<n>  alimenta OGNI ingresso di bordo con la costante n a ogni ciclo,
#              cioe' la condizione al contorno di Dirichlet dello stencil.
#              Senza, il bordo resta vuoto: aperto, non arriva mai niente.
#   Le uscite di perimetro sono SEMPRE drenate (un OUT che nessuno consuma
#   inchioda la cella): si stampa solo il totale, perche' un kernel come lo
#   stencil ne emette migliaia. Per un valore preciso, o per un contorno che
#   varia per cella o nel tempo, si usa grid_push/grid_pop da C: vedi
#   tests/test_bordo.c e tests/test_jacobi.c.
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

.PHONY: all stella asm test test-channel test-mem test-prodcons test-chain test-broadcast test-bordo test-reduce test-jacobi run step clean

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

test: test-channel test-mem test-prodcons test-chain test-broadcast test-bordo test-reduce test-jacobi

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

# Forme di griglia su cui il broadcast deve coprire TUTTE le celle.
# 1x6 e 6x1 isolano i due cicli di cablaggio di grid_init (E-O e N-S). 
# Le altre li mescolano.
FORME  = 1x1 1x6 6x1 3x4 4x4
VALORE ?= 7

# Contorno di Dirichlet per lo stencil. 64 e' una potenza di 2: la soluzione
# esatta e' 64 su tutta la griglia, quindi si legge a occhio se il campo e'
# arrivato o si e' fermato prima.
VALORE_BORDO ?= 64

build/test_broadcast: tests/test_broadcast.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_broadcast.c src/core.c src/grid.c src/elf.c

test-broadcast: build/test_broadcast asm/broadcast.s
	@$(AS) $(ASFLAGS) --defsym VALORE=$(VALORE) -o build/bc.o asm/broadcast.s || exit 1
	@for f in $(FORME); do \
	  ./build/test_broadcast build/bc.o $${f%x*} $${f#*x} $(VALORE) || exit 1; \
	done

# memtest.s si autoverifica e lascia il verdetto in s1, quindi gli basta
# l'harness di broadcast: quello e' gia' un "esegui su RxC e asserisci s1",
# il nome dice broadcast solo perche' e' nato li'.
test-mem: build/test_broadcast asm/memtest.s
	@$(AS) $(ASFLAGS) -o build/memtest.o asm/memtest.s || exit 1
	@./build/test_broadcast build/memtest.o 1 1 $$((0x11223344))

# Stesse forme del broadcast: 1x6 e 6x1 isolano i due estremi (catena di un solo
# hop su ogni colonna / colonna unica lunga), le altre li mescolano.
build/test_bordo: tests/test_bordo.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_bordo.c src/core.c src/grid.c src/elf.c

test-bordo: build/test_bordo asm/bordo.s
	@$(AS) $(ASFLAGS) -o build/bordo.o asm/bordo.s || exit 1
	@for f in $(FORME); do \
	  ./build/test_bordo build/bordo.o $${f%x*} $${f#*x} $(VALORE) || exit 1; \
	done

# Il totale atteso lo ricava il test dalla forma, quindi qui bastano le forme.
build/test_reduce: tests/test_reduce.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_reduce.c src/core.c src/grid.c src/elf.c

test-reduce: build/test_reduce asm/reduce.s
	@$(AS) $(ASFLAGS) -o build/reduce.o asm/reduce.s || exit 1
	@for f in $(FORME); do \
	  ./build/test_reduce build/reduce.o $${f%x*} $${f#*x} || exit 1; \
	done

# Jacobi: ITER va scelto sulla tolleranza, e la tabella che il test stampa dice
# a quale iterazione il campo raggiunge il punto fisso. 64 basta a far
# convergere tutte le FORME con BORDO=64, cosi' scatta anche l'asserzione che
# il punto fisso e' la soluzione vera (e non quella troncata).
# SEMI: entrambi i campi iniziali previsti — 0 interno freddo, 1 campo r+c.
ITER  ?= 64
SEMI  ?= 0 1

build/test_jacobi: tests/test_jacobi.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_jacobi.c src/core.c src/grid.c src/elf.c

test-jacobi: build/test_jacobi asm/jacobi.s
	@for s in $(SEMI); do \
	  $(AS) $(ASFLAGS) --defsym ITER=$(ITER) --defsym SEME=$$s \
	      -o build/jacobi_$$s.o asm/jacobi.s || exit 1; \
	  for f in $(FORME); do \
	    ./build/test_jacobi build/jacobi_$$s.o $${f%x*} $${f#*x} $(VALORE_BORDO) \
	        $(ITER) $$s || exit 1; \
	  done; \
	done

build:
	mkdir -p build

clean:
	rm -rf build
