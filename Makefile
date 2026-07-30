# NESO - simulatore di array RISC_V
#
#   make                                 compila build/stella
#   make asm                             assembla ogni asm/*.s in build/
#   make run  P=<prog> [R= C= N= DEFS=]  esegue sulla griglia, fila dritto
#   make step P=<prog> [R= C= N= DEFS=]  esegue un ciclo per INVIO
#   make test [QUANTI=10]                tutta la suite: canale, kernel, bordo,
#                                        stencil, matmul
#   make test-<nome> [QUANTI=10]         un solo pezzo (prodcons, chain, nobp, ...)
#   make clean
#
#   P      nome del .s in asm/, senza estensione     (es. P=prodcons)
#   R C    righe e colonne della griglia             (default 1 2)
#   N      tetto sui cicli: se lo tocca, l'esecuzione è troncata (default 200)
#   DEFS   simboli passati all'assembler             (es. DEFS="--defsym RITARDO=25")
#
# I/O di bordo (l'host fa il vicino che le celle di perimetro non hanno):
#   BORDO=<n>  alimenta OGNI ingresso di bordo con la costante n a ogni ciclo,
#              cioè la condizione al contorno di Dirichlet dello stencil(qui Jacobi).
#              Senza, il bordo resta vuoto: aperto, non arriva mai niente.
#   Le uscite di perimetro sono SEMPRE drenate (un OUT che nessuno consuma
#   inchioda la cella): si stampa solo il totale, perché un kernel come lo
#   stencil ne emette migliaia. Per un valore preciso, o per un contorno che
#   varia per cella o nel tempo, si usa grid_push/grid_pop da C: vedi
#   tests/test_bordo.c e tests/test_jacobi.c.
#
# I parametri dei programmi (RITARDO, QUANTI) stanno nei .s dentro .ifndef,
# quindi hanno un default nel file e si sovrascrivono da qui con DEFS.

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -Isrc
AS      = riscv64-unknown-elf-as

# rv32i di proposito: l'assemblatore rifiuta così tutto quello che
# l'interprete non implementa. Il vincolo però è per-kernel, non globale —
# matmul.s ha bisogno di `mul` e alza ARCH da solo (vedi test-matmul), senza
# concedere la M a tutti gli altri.
#   make run P=matmul ARCH=rv32im R=2 C=2 N=500
ARCH    ?= rv32i
ASFLAGS  = -march=$(ARCH) -mabi=ilp32 -Iasm

SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=build/%.o)

ASM_SRC = $(wildcard asm/*.s)
ASM_OBJ = $(ASM_SRC:asm/%.s=build/%.o)

.PHONY: all stella asm test test-channel test-mem test-prodcons test-chain test-broadcast test-bordo test-reduce test-jacobi test-matmul test-nobp dati run step clean

all stella: build/stella

build/stella: $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ $(OBJ)

build/%.o: src/%.c src/*.h | build
	$(CC) $(CFLAGS) -c $< -o $@

asm: $(ASM_OBJ)

build/matmul.o: ARCH = rv32im     # l'unico che usa la M, così `make asm` regge
build/%.o: asm/%.s | build
	$(AS) $(ASFLAGS) $(DEFS) -o $@ $<

# Riassemblano sempre: cambiando solo DEFS il .s non cambia e make non
# rifarebbe il .o. MODO è una variabile target-specific: unica ricetta, due
# modalità (STEP lo legge run_grid in main.c).
R ?= 1
C ?= 2
N ?= 200
run:  MODO =
step: MODO = STEP=1
run step: build/stella
	$(AS) $(ASFLAGS) $(DEFS) -o build/$(P).o asm/$(P).s
	$(MODO) ./build/stella build/$(P).o $(R) $(C) $(N)

test: test-channel test-mem test-prodcons test-chain test-broadcast test-bordo test-reduce test-jacobi test-matmul

test-channel: tests/test_channel.c src/channel.h | build
	$(CC) $(CFLAGS) -o build/test_channel tests/test_channel.c
	./build/test_channel

# Il risultato non deve dipendere né dalla velocità relativa dei nodi
# (RITARDI) né dalla lunghezza della catena (COLONNE). La somma attesa è
# derivata da QUANTI, non scritta a mano: make test QUANTI=10
RITARDI = 0 1 4 8 25 60
COLONNE = 2 3 5 12
QUANTI ?= 5
ATTESA  = $$(( $(QUANTI) * ($(QUANTI) + 1) / 2 ))

# un solo binario di test per entrambi i programmi: prodcons.s è il caso C=2
build/test_catena: tests/test_catena.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_catena.c src/core.c src/grid.c src/elf.c

test-prodcons: build/test_catena asm/prodcons.s
	@for d in $(RITARDI); do \
	  $(AS) $(ASFLAGS) --defsym RITARDO=$$d --defsym QUANTI=$(QUANTI) \
	      -o build/pc_$$d.o asm/prodcons.s || exit 1; \
	  printf 'prodcons,%s,%s,' $$d $(QUANTI) >&2; \
	  ./build/test_catena build/pc_$$d.o 2 $(ATTESA) || exit 1; \
	done

# La misura del ready bit: stesso kernel, stesso sweep, senza controllo di
# flusso (NOBP=1, vedi il case PCIO in core.c). Una riga per RITARDO con la
# somma MISURATA: dove si stacca da $(ATTESA), il lockstep ha perso dati.
# Fuori da `make test` di proposito — qui il fallimento è il risultato.
test-nobp: build/test_catena asm/prodcons.s
	@for d in $(RITARDI); do \
	  $(AS) $(ASFLAGS) --defsym RITARDO=$$d --defsym QUANTI=$(QUANTI) \
	      -o build/pc_$$d.o asm/prodcons.s || exit 1; \
	  printf 'prodcons,%s,%s,' $$d $(QUANTI) >&2; \
	  NOBP=1 ./build/test_catena build/pc_$$d.o 2 -1 || exit 1; \
	done

test-chain: build/test_catena asm/chain.s
	@for d in $(RITARDI); do \
	  $(AS) $(ASFLAGS) --defsym RITARDO=$$d --defsym QUANTI=$(QUANTI) \
	      -o build/ch_$$d.o asm/chain.s || exit 1; \
	  for c in $(COLONNE); do \
	    printf 'chain,%s,%s,' $$d $(QUANTI) >&2; \
	    ./build/test_catena build/ch_$$d.o $$c $(ATTESA) || exit 1; \
	  done; \
	done

# Forme di griglia su cui il broadcast deve coprire TUTTE le celle.
# 1x6 e 6x1 isolano i due cicli di cablaggio di grid_init (E-O e N-S). 
# Le altre li mescolano.
FORME  = 1x1 1x6 6x1 3x4 4x4 8x8 12x12
VALORE ?= 7

# Contorno di Dirichlet per lo stencil. 64 è una potenza di 2: la soluzione
# esatta è 64 su tutta la griglia, quindi si legge a occhio se il campo è
# arrivato o si è fermato prima.
VALORE_BORDO ?= 64

build/test_broadcast: tests/test_broadcast.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_broadcast.c src/core.c src/grid.c src/elf.c

test-broadcast: build/test_broadcast asm/broadcast.s
	@$(AS) $(ASFLAGS) --defsym VALORE=$(VALORE) -o build/bc.o asm/broadcast.s || exit 1
	@for f in $(FORME); do \
	  printf 'broadcast,' >&2; \
	  ./build/test_broadcast build/bc.o $${f%x*} $${f#*x} $(VALORE) || exit 1; \
	done

# memtest.s si autoverifica e lascia il verdetto in s1, quindi gli basta
# l'harness di broadcast: quello è già un "esegui su RxC e asserisci s1",
# il nome dice broadcast solo perché è nato lì.
test-mem: build/test_broadcast asm/memtest.s
	@$(AS) $(ASFLAGS) -o build/memtest.o asm/memtest.s || exit 1
	@printf 'memtest,' >&2
	@./build/test_broadcast build/memtest.o 1 1 $$((0x11223344))

# Stesse forme del broadcast: 1x6 e 6x1 isolano i due estremi (catena di un solo
# hop su ogni colonna / colonna unica lunga), le altre li mescolano.
build/test_bordo: tests/test_bordo.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_bordo.c src/core.c src/grid.c src/elf.c

test-bordo: build/test_bordo asm/bordo.s
	@$(AS) $(ASFLAGS) -o build/bordo.o asm/bordo.s || exit 1
	@for f in $(FORME); do \
	  printf 'bordo,' >&2; \
	  ./build/test_bordo build/bordo.o $${f%x*} $${f#*x} $(VALORE) || exit 1; \
	done

# Il totale atteso lo ricava il test dalla forma, quindi qui bastano le forme.
build/test_reduce: tests/test_reduce.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_reduce.c src/core.c src/grid.c src/elf.c

test-reduce: build/test_reduce asm/reduce.s
	@$(AS) $(ASFLAGS) -o build/reduce.o asm/reduce.s || exit 1
	@for f in $(FORME); do \
	  printf 'reduce,' >&2; \
	  ./build/test_reduce build/reduce.o $${f%x*} $${f#*x} || exit 1; \
	done

# Jacobi: ITER va scelto sulla tolleranza, e la tabella che il test stampa dice
# a quale iterazione il campo raggiunge il punto fisso. 64 basta a far
# convergere tutte le FORME con BORDO=64, così scatta anche l'asserzione che
# il punto fisso è la soluzione vera (e non quella troncata).
# SEMI: entrambi i campi iniziali previsti — 0 interno freddo, 1 campo r+c.
ITER  ?= 128
SEMI  ?= 0 1

build/test_jacobi: tests/test_jacobi.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_jacobi.c src/core.c src/grid.c src/elf.c

test-jacobi: build/test_jacobi asm/jacobi.s
	@for s in $(SEMI); do \
	  $(AS) $(ASFLAGS) --defsym ITER=$(ITER) --defsym SEME=$$s \
	      -o build/jacobi_$$s.o asm/jacobi.s || exit 1; \
	  for f in $(FORME); do \
	    printf 'jacobi,' >&2; \
	    ./build/test_jacobi build/jacobi_$$s.o $${f%x*} $${f#*x} $(VALORE_BORDO) \
	        $(ITER) $$s || exit 1; \
	  done; \
	done

# Matmul: il caso d'uso della specifica (sezione 6). La griglia RxC è la forma
# del risultato, K è la lunghezza del prodotto interno — l'unico parametro,
# perché i dati arrivano dal bordo e non dalla .data.
# è l'unico kernel che richiede la M (`mul`), quindi ARCH sale solo qui.
KAPPA ?= 1 4 7

build/test_matmul: tests/test_matmul.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_matmul.c src/core.c src/grid.c src/elf.c

test-matmul: ARCH = rv32im
test-matmul: build/test_matmul asm/matmul.s
	@for k in $(KAPPA); do \
	  $(AS) $(ASFLAGS) --defsym K=$$k -o build/matmul_$$k.o asm/matmul.s || exit 1; \
	  for f in $(FORME); do \
	    printf 'matmul,%s,' $$k >&2; \
	    ./build/test_matmul build/matmul_$$k.o $${f%x*} $${f#*x} $$k || exit 1; \
	  done; \
	done

# Le misure per la relazione. Stesse sweep dei test, stessa uscita: i test
# stampano già una riga CSV per run su stderr, qui si aggiunge solo
# l'intestazione e si raccoglie. CSV=1 zittisce le stampe multiriga
# (mappa dell'attesa del broadcast, traccia dei delta di Jacobi) che
# spezzerebbero il formato. Colonne documentate in docs/dati/README.md.
#   make dati                       rigenera tutto
#   column -t -s, docs/dati/*.csv   per leggerle a occhio
DATI = docs/dati
SPINE = cicli,ritentativi,attese

dati: build/test_catena build/test_broadcast build/test_bordo build/test_reduce build/test_jacobi build/test_matmul
	@mkdir -p $(DATI)
	@echo 'kernel,k,righe,colonne,$(SPINE),spinte_rifiutate' > $(DATI)/matmul.csv
	@CSV=1 $(MAKE) -s test-matmul 2>> $(DATI)/matmul.csv
	@echo 'kernel,ritardo,quanti,righe,colonne,backpressure,$(SPINE),somma' > $(DATI)/catena.csv
	@CSV=1 $(MAKE) -s test-prodcons test-chain test-nobp 2>> $(DATI)/catena.csv
	@echo 'kernel,righe,colonne,$(SPINE),valore' > $(DATI)/broadcast.csv
	@CSV=1 $(MAKE) -s test-broadcast test-mem 2>> $(DATI)/broadcast.csv
	@echo 'kernel,righe,colonne,$(SPINE),valore,usciti' > $(DATI)/bordo.csv
	@CSV=1 $(MAKE) -s test-bordo 2>> $(DATI)/bordo.csv
	@echo 'kernel,righe,colonne,$(SPINE),totale' > $(DATI)/reduce.csv
	@CSV=1 $(MAKE) -s test-reduce 2>> $(DATI)/reduce.csv
	@echo 'kernel,righe,colonne,$(SPINE),bordo,iter,seme,usciti,k_finale,delta' > $(DATI)/jacobi.csv
	@CSV=1 $(MAKE) -s test-jacobi 2>> $(DATI)/jacobi.csv
	@wc -l $(DATI)/*.csv

build:
	mkdir -p build

clean:
	rm -rf build
