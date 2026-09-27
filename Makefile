# MAKEFILE
#
#   make                                 compila build/neso
#   make asm                             assembla ogni asm/*.s in build/
#   make run  P=<prog> [R= C= N= DEFS=]  esegue sulla griglia
#   make step P=<prog> [R= C= N= DEFS=]  un ciclo per INVIO
#   make test [Q=10]                     tutta la suite
#   make test-<nome> [Q=10]              un solo test (prodcons, chain, ...)
#   make clean
#
#   P      nome del .s in asm/, senza estensione     (es. P=prodcons)
#   R C    righe e colonne della griglia             (default 1 2)
#   N      tetto sui cicli                           (default 200)
#   DEFS   simboli per l'assembler         (es. DEFS="--defsym RITARDO=25")
#   BORDO  costante su ogni ingresso di bordo a ogni ciclo; senza, bordo
#          vuoto. Le uscite di perimetro sono sempre drenate.
#
# Variabili d'ambiente del simulatore:
#   TRACE=1            traccia per istruzione (lenta)
#   NOBP=1             canali senza controllo di flusso (case PCIO in risc.c)
#   OMP_NUM_THREADS=n  thread di grid_step
#
# Build seriale, senza libgomp:
#   make clean && make CFLAGS="-Wall -Wextra -std=c11 -Isrc -O2"

# ===== TOOLCHAIN E FLAG ====================================================

CC      = gcc

# -fopenmp serve in compilazione e in link; senza, le #pragma sono ignorate
# e la stessa sorgente compila seriale.
CFLAGS  = -Wall -Wextra -std=c11 -Isrc -O2 -fopenmp
AS      = riscv64-unknown-elf-as

ARCH    ?= rv32i
ASFLAGS  = -march=$(ARCH) -mabi=ilp32 -Iasm

# ===== SORGENTI ============================================================

SRC = $(wildcard src/*.c)
OBJ = $(SRC:src/%.c=build/%.o)

ASM_SRC = $(wildcard asm/*.s)
ASM_OBJ = $(ASM_SRC:asm/%.s=build/%.o)

# ===== COSTRUZIONE =========================================================

.PHONY: all neso asm

all neso: build/neso

build/neso: $(OBJ) | build
	$(CC) $(CFLAGS) -o $@ $(OBJ)

build/%.o: src/%.c src/*.h | build
	$(CC) $(CFLAGS) -c $< -o $@

asm: $(ASM_OBJ)

build/matmul.o: ARCH = rv32im     # l'unico che usa la M, così 'make asm' funziona
build/%.o: asm/%.s | build
	$(AS) $(ASFLAGS) $(DEFS) -o $@ $<

# ===== ESECUZIONE ==========================================================
.PHONY: run step

R ?= 1
C ?= 2
N ?= 200
run:  MODO =
step: MODO = STEP=1
run step: build/neso
	$(AS) $(ASFLAGS) $(DEFS) -o build/$(P).o asm/$(P).s
	$(MODO) ./build/neso build/$(P).o $(R) $(C) $(N)

# ===== TEST ================================================================
# test-nobp è di proposito fuori da test: lì il fallimento è il risultato.
.PHONY: test test-channel test-mem test-prodcons test-nobp test-chain \
        test-broadcast test-bordo test-reduce test-jacobi test-matmul \
        test-pesante

test: test-channel test-mem test-prodcons test-chain test-broadcast test-bordo test-reduce test-jacobi test-matmul test-pesante

# --- canale ----------------------------------------------------------------
test-channel: tests/test_channel.c src/channel.h | build
	$(CC) $(CFLAGS) -o build/test_channel tests/test_channel.c
	./build/test_channel

# --- catena: prodcons, nobp, chain -----------------------------------------
# Somma attesa Q*(Q+1)/2 per ogni RITARDO e ogni lunghezza.
RITARDI = 0 1 4 8 25 60
COLONNE = 2 3 5 12
Q ?= 5
ATTESA  = $$(( $(Q) * ($(Q) + 1) / 2 ))

# un solo binario di test per entrambi i programmi: prodcons.s è il caso C=2
build/test_catena: tests/test_catena.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_catena.c src/risc.c src/grid.c src/elf.c

test-prodcons: build/test_catena asm/prodcons.s
	@for d in $(RITARDI); do \
	  $(AS) $(ASFLAGS) --defsym RITARDO=$$d --defsym Q=$(Q) \
	      -o build/pc_$$d.o asm/prodcons.s || exit 1; \
	  printf 'prodcons,%s,%s,' $$d $(Q) >&2; \
	  ./build/test_catena build/pc_$$d.o 2 $(ATTESA) || exit 1; \
	done

# Stesso sweep con NOBP=1: stampa la somma misurata invece di asserirla.
test-nobp: build/test_catena asm/prodcons.s
	@for d in $(RITARDI); do \
	  $(AS) $(ASFLAGS) --defsym RITARDO=$$d --defsym Q=$(Q) \
	      -o build/pc_$$d.o asm/prodcons.s || exit 1; \
	  printf 'prodcons,%s,%s,' $$d $(Q) >&2; \
	  NOBP=1 ./build/test_catena build/pc_$$d.o 2 -1 || exit 1; \
	done

test-chain: build/test_catena asm/chain.s
	@for d in $(RITARDI); do \
	  $(AS) $(ASFLAGS) --defsym RITARDO=$$d --defsym Q=$(Q) \
	      -o build/ch_$$d.o asm/chain.s || exit 1; \
	  for c in $(COLONNE); do \
	    printf 'chain,%s,%s,' $$d $(Q) >&2; \
	    ./build/test_catena build/ch_$$d.o $$c $(ATTESA) || exit 1; \
	  done; \
	done

# --- griglia: broadcast, memoria, bordo, reduce ----------------------------
# 1x6 e 6x1 isolano i cablaggi E-O e N-S di grid_init.
FORME  = 1x1 1x6 6x1 3x4 4x4 8x8 12x12
VALORE ?= 7

# Contorno dello stencil: la soluzione esatta vale VALORE_BORDO ovunque.
VALORE_BORDO ?= 64

build/test_broadcast: tests/test_broadcast.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_broadcast.c src/risc.c src/grid.c src/elf.c

test-broadcast: build/test_broadcast asm/broadcast.s
	@$(AS) $(ASFLAGS) --defsym VALORE=$(VALORE) -o build/bc.o asm/broadcast.s || exit 1
	@for f in $(FORME); do \
	  printf 'broadcast,' >&2; \
	  ./build/test_broadcast build/bc.o $${f%x*} $${f#*x} $(VALORE) || exit 1; \
	done

# memtest.s si autoverifica in s1: basta l'harness di broadcast.
test-mem: build/test_broadcast asm/memtest.s
	@$(AS) $(ASFLAGS) -o build/memtest.o asm/memtest.s || exit 1
	@printf 'memtest,' >&2
	@./build/test_broadcast build/memtest.o 1 1 $$((0x11223344))

build/test_bordo: tests/test_bordo.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_bordo.c src/risc.c src/grid.c src/elf.c

test-bordo: build/test_bordo asm/bordo.s
	@$(AS) $(ASFLAGS) -o build/bordo.o asm/bordo.s || exit 1
	@for f in $(FORME); do \
	  printf 'bordo,' >&2; \
	  ./build/test_bordo build/bordo.o $${f%x*} $${f#*x} $(VALORE) || exit 1; \
	done

# Il totale atteso lo ricava il test dalla forma.
build/test_reduce: tests/test_reduce.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_reduce.c src/risc.c src/grid.c src/elf.c

test-reduce: build/test_reduce asm/reduce.s
	@$(AS) $(ASFLAGS) -o build/reduce.o asm/reduce.s || exit 1
	@for f in $(FORME); do \
	  printf 'reduce,' >&2; \
	  ./build/test_reduce build/reduce.o $${f%x*} $${f#*x} || exit 1; \
	done

# --- stencil ---------------------------------------------------------------
# ITER fa convergere tutte le FORME, così il test asserisce anche la
# soluzione esatta. SEMI: 0 = interno freddo, 1 = campo r+c.
ITER  ?= 128
SEMI  ?= 0 1

build/test_jacobi: tests/test_jacobi.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_jacobi.c src/risc.c src/grid.c src/elf.c

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

# --- matmul ----------------------------------------------------------------
# K = lunghezza del prodotto interno; la griglia ha la forma di C.
K ?= 1 4 7

build/test_matmul: tests/test_matmul.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_matmul.c src/risc.c src/grid.c src/elf.c

test-matmul: ARCH = rv32im
test-matmul: build/test_matmul asm/matmul.s
	@for k in $(K); do \
	  $(AS) $(ASFLAGS) --defsym K=$$k -o build/matmul_$$k.o asm/matmul.s || exit 1; \
	  for f in $(FORME); do \
	    printf 'matmul,%s,' $$k >&2; \
	    ./build/test_matmul build/matmul_$$k.o $${f%x*} $${f#*x} $$k || exit 1; \
	  done; \
	done

# --- kernel sintetico ------------------------------------------------------
# Solo correttezza, con ITER e PESO bassi: lo sweep è in 'scala'.
ITER_PESANTE ?= 8
PESO         ?= 32

build/test_pesante: tests/test_pesante.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_pesante.c src/risc.c src/grid.c src/elf.c

test-pesante: build/test_pesante asm/pesante.s
	@$(AS) $(ASFLAGS) --defsym ITER=$(ITER_PESANTE) --defsym PESO=$(PESO) \
	    -o build/pesante.o asm/pesante.s || exit 1
	@for f in $(FORME); do \
	  printf 'pesante,' >&2; \
	  ./build/test_pesante build/pesante.o $${f%x*} $${f#*x} $(VALORE_BORDO) \
	      $(ITER_PESANTE) $(PESO) || exit 1; \
	done

# ===== MISURE ==============================================================
.PHONY: dati

# CSV per la tesi: una riga per run dagli harness, CSV=1 toglie le stampe
# multiriga. Colonne in docs/dati/README.md.
DATI = docs/dati
CAT = cicli,ritentativi,attese

dati: build/test_catena build/test_broadcast build/test_bordo build/test_reduce build/test_jacobi build/test_matmul
	@mkdir -p $(DATI)
	@echo 'kernel,k,righe,colonne,$(CAT),spinte_rifiutate' > $(DATI)/matmul.csv
	@CSV=1 $(MAKE) -s test-matmul 2>> $(DATI)/matmul.csv
	@echo 'kernel,ritardo,quanti,righe,colonne,backpressure,$(CAT),somma' > $(DATI)/catena.csv
	@CSV=1 $(MAKE) -s test-prodcons test-chain test-nobp 2>> $(DATI)/catena.csv
	@echo 'kernel,righe,colonne,$(CAT),valore' > $(DATI)/broadcast.csv
	@CSV=1 $(MAKE) -s test-broadcast test-mem 2>> $(DATI)/broadcast.csv
	@echo 'kernel,righe,colonne,$(CAT),valore,usciti' > $(DATI)/bordo.csv
	@CSV=1 $(MAKE) -s test-bordo 2>> $(DATI)/bordo.csv
	@echo 'kernel,righe,colonne,$(CAT),totale' > $(DATI)/reduce.csv
	@CSV=1 $(MAKE) -s test-reduce 2>> $(DATI)/reduce.csv
	@echo 'kernel,righe,colonne,$(CAT),bordo,iter,seme,usciti,k_finale,delta' > $(DATI)/jacobi.csv
	@CSV=1 $(MAKE) -s test-jacobi 2>> $(DATI)/jacobi.csv
	@echo 'kernel,righe,colonne,$(CAT),bordo,iter,peso,usciti' > $(DATI)/pesante.csv
	@CSV=1 $(MAKE) -s test-pesante 2>> $(DATI)/pesante.csv
	@wc -l $(DATI)/*.csv

# --- tempi di esecuzione ---------------------------------------------------
# Sweep di 'dati' ripetuto per ogni THREAD, riga scritta dal cronometro di
# grid.c (TEMPI=<file>). Es.: make tempi THREAD='1 8'
.PHONY: tempi
THREAD  = 1 2 4 8 16
KERNELS = chain broadcast bordo reduce jacobi matmul

tempi: build/test_catena build/test_broadcast build/test_bordo build/test_reduce build/test_jacobi build/test_matmul
	@mkdir -p $(DATI)
	@echo 'kernel,righe,colonne,thread,cicli,t_tot,t_step,seriale' > $(DATI)/tempi.csv
	@for t in $(THREAD); do \
	  for k in $(KERNELS); do \
	    OMP_NUM_THREADS=$$t KERNEL=$$k TEMPI=$(DATI)/tempi.csv CSV=1 \
	        $(MAKE) -s test-$$k 2>/dev/null || exit 1; \
	  done; \
	done
	@column -t -s, $(DATI)/tempi.csv

# --- il punto di pareggio della parallelizzazione ---------------------------
# Punto di pareggio di OpenMP al variare di PESO, con pesante.s.
# Ripetizioni interlacciate (PESO più interno) perché la deriva della
# macchina non si confonda con PESO; in analisi si prende il minimo.
#   make scala SCALA_PESO=32       una sola curva
.PHONY: scala
SCALA_FORME  = 8x8 16x16 24x24 32x32 40x40 48x48 56x56 64x64 96x96 128x128 192x192 256x256
SCALA_THREAD = 1 2 4 8 16
SCALA_PESO   = 1 8 32
SCALA_REP    = 1 2 3 4 5 6 7 8 9
SCALA_ITER  ?= 8

scala: build/test_pesante
	@mkdir -p $(DATI)
	@echo 'kernel,righe,colonne,thread,cicli,t_tot,t_step,seriale' > $(DATI)/scala.csv
	@for p in $(SCALA_PESO); do \
	  $(AS) $(ASFLAGS) --defsym ITER=$(SCALA_ITER) --defsym PESO=$$p \
	      -o build/pesante_$$p.o asm/pesante.s || exit 1; \
	done
	@for r in $(SCALA_REP); do \
	  for f in $(SCALA_FORME); do \
	    for t in $(SCALA_THREAD); do \
	      for p in $(SCALA_PESO); do \
	        OMP_NUM_THREADS=$$t KERNEL=pesante-p$$p TEMPI=$(DATI)/scala.csv CSV=1 \
	            ./build/test_pesante build/pesante_$$p.o $${f%x*} $${f#*x} \
	            $(VALORE_BORDO) $(SCALA_ITER) $$p 2>/dev/null || exit 1; \
	      done; \
	    done; \
	  done; \
	  printf 'ripetizione %s fatta\n' $$r; \
	done
	@wc -l $(DATI)/scala.csv
	@$(MAKE) -s grafici

# Coordinate del grafico di tesi, generate da scala.csv.
.PHONY: grafici
grafici:
	@python3 $(DATI)/grafici.py

# --- il costo della sola località -------------------------------------------
# Costo per cella-ciclo al crescere della griglia, a un thread.
# FORMA:ITER tarati per ~2 s a run; da rifare se cambia MEM_PESO.
.PHONY: memoria
MEM_FORME = 16x16:1000 32x32:300 48x48:90 64x64:37 96x96:15 128x128:8 192x192:3 256x256:2
MEM_REP   = 1 2 3
MEM_PESO  = 32

memoria: build/test_pesante
	@mkdir -p $(DATI)
	@echo 'kernel,righe,colonne,thread,cicli,t_tot,t_step,seriale' > $(DATI)/memoria.csv
	@for r in $(MEM_REP); do \
	  for fi in $(MEM_FORME); do \
	    f=$${fi%:*}; i=$${fi#*:}; \
	    $(AS) $(ASFLAGS) --defsym ITER=$$i --defsym PESO=$(MEM_PESO) \
	        -o build/pesante_mem.o asm/pesante.s || exit 1; \
	    OMP_NUM_THREADS=1 KERNEL=pesante-mem TEMPI=$(DATI)/memoria.csv CSV=1 \
	        ./build/test_pesante build/pesante_mem.o $${f%x*} $${f#*x} \
	        $(VALORE_BORDO) $$i $(MEM_PESO) 2>/dev/null || exit 1; \
	  done; \
	  printf 'ripetizione %s fatta\n' $$r; \
	done
	@wc -l $(DATI)/memoria.csv

# --- costo di apertura delle regioni parallele -------------------------------
# Programma a sé, non il simulatore: è quello della nota di §5.7.2 e della
# colonna "due regioni" di tab:crossover. I numeri dipendono dalla sessione.
.PHONY: regioni
regioni: build/bench_regioni
	@./build/bench_regioni

build/bench_regioni: docs/bench_regioni.c | build
	$(CC) $(CFLAGS) -o $@ $<

# --- l'intensità di calcolo a parità di cicli -------------------------------
# Speedup a cicli pari: PESO=1/ITER=62 e PESO=32/ITER=8 fanno entrambi 2624
# cicli (lo verifica l'awk finale).
.PHONY: controllo
CTL_FORME  = 32x32 48x48
CTL_THREAD = 1 8
CTL_REP    = 1 2 3 4 5
CTL_CASI   = 1:62 32:8

controllo: build/test_pesante
	@mkdir -p $(DATI)
	@echo 'kernel,righe,colonne,thread,cicli,t_tot,t_step,seriale' > $(DATI)/controllo.csv
	@for c in $(CTL_CASI); do \
	  p=$${c%:*}; i=$${c#*:}; \
	  $(AS) $(ASFLAGS) --defsym ITER=$$i --defsym PESO=$$p \
	      -o build/pesante_c$$p.o asm/pesante.s || exit 1; \
	done
	@for r in $(CTL_REP); do \
	  for f in $(CTL_FORME); do \
	    for t in $(CTL_THREAD); do \
	      for c in $(CTL_CASI); do \
	        p=$${c%:*}; i=$${c#*:}; \
	        OMP_NUM_THREADS=$$t KERNEL=pesante-p$$p-i$$i TEMPI=$(DATI)/controllo.csv CSV=1 \
	            ./build/test_pesante build/pesante_c$$p.o $${f%x*} $${f#*x} \
	            $(VALORE_BORDO) $$i $$p 2>/dev/null || exit 1; \
	      done; \
	    done; \
	  done; \
	done
	@awk -F, 'NR>1 {c[$$5]++} END {if (length(c) != 1) {print "ERRORE: i cicli non sono pari fra i due casi"; exit 1}; for (k in c) print "cicli pari su tutte le righe:", k}' $(DATI)/controllo.csv
	@wc -l $(DATI)/controllo.csv

# ===== UTILITY =============================================================

.PHONY: clean

build:
	mkdir -p build

clean:
	rm -rf build
