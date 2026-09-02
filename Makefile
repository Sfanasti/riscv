# MAKEFILE 
#
#   make                                 compila build/neso
#   make asm                             assembla ogni asm/*.s in build/
#   make run  P=<prog> [R= C= N= DEFS=]  esegue sulla griglia senza interruzioni
#   make step P=<prog> [R= C= N= DEFS=]  esegue un ciclo per INVIO
#   make test [Q=10]                tutta la suite: canale, kernel, bordo,
#                                        stencil, matmul
#   make test-<nome> [Q=10]         un solo pezzo (prodcons, chain, nobp, ...)
#   make clean
#
#   P      nome del .s in asm/, senza estensione     (es. P=prodcons)
#   R C    righe e colonne della griglia             (default 1 2)
#   N      tetto sui cicli: se lo tocca tronca l'esecuzione (default 200)
#   DEFS   simboli passati all'assembler             (es. DEFS="--defsym RITARDO=25")
#
# I/O di bordo (l'host fa il vicino che le celle di perimetro non hanno):
#   BORDO=<n>  alimenta OGNI ingresso di bordo con la costante n a ogni ciclo,
#              cioè la condizione al contorno di Dirichlet dello stencil(qui Jacobi).
#              Senza, il bordo resta vuoto.
#   Le uscite di perimetro sono SEMPRE drenate (un OUT che nessuno consuma
#   inchioda la cella): si stampa solo il totale, perché un kernel come lo
#   stencil ne emette migliaia. Per un valore preciso, o per un contorno che
#   varia per cella o nel tempo, si usa grid_push/grid_pop da C: si veda
#   tests/test_bordo.c e tests/test_jacobi.c.
#
# I parametri dei programmi (RITARDO, Q) stanno nei .s dentro .ifndef,
# quindi hanno un default nel file e si sovrascrivono da qui con DEFS.
#
# Variabili d'ambiente lette dal simulatore:
#   TRACE=1            riaccende la traccia per istruzione (spenta di default:
#                      costava il 58% del tempo, e con grid_step parallelo il
#                      lock di stdout serializzerebbe tutti i thread)
#   NOBP=1             canale senza handshake, si veda il case PCIO in risc.c
#   OMP_NUM_THREADS=n  quanti thread usa grid_step. =1 e' il riferimento per
#                      calcolare lo speedup: stesso binario, stesso codice.
#
# Per una build seriale pura (senza libgomp), una volta sola e senza toccare
# niente:  make clean && make CFLAGS="-Wall -Wextra -std=c11 -Isrc -O2"

# ===== TOOLCHAIN E FLAG ====================================================

CC      = gcc

# -O2      la traccia spenta e l'ottimizzazione valgono 3,5x sul seriale
#          (misurato: 122 -> 52 -> 35 ns per istruzione simulata)
# -fopenmp serve sia in compilazione (traduce le #pragma di grid_step) sia in
#          link (libgomp): CFLAGS è usata in entrambe, quindi basta qui.
#          Senza, le #pragma restano direttive sconosciute e vengono ignorate:
#          la stessa sorgente compila seriale, senza nessun #ifdef.
CFLAGS  = -Wall -Wextra -std=c11 -Isrc -O2 -fopenmp
AS      = riscv64-unknown-elf-as

# matmul.s ha bisogno di 'mul' e alza ARCH da solo (si veda test-matmul), senza
# concedere la M a tutti gli altri.
#   make run P=matmul ARCH=rv32im R=2 C=2 N=500
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
# Il risultato non deve dipendere né dalla velocità relativa dei nodi
# (RITARDI) né dalla lunghezza della catena (COLONNE). La somma attesa è
# derivata da Q: make test Q=10
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

# Misura del ready bit: stesso kernel, stesso sweep, senza controllo di
# flusso (NOBP=1, vedi il case PCIO in risc.c). Una riga per RITARDO con la
# somma MISURATA: dove si stacca da $(ATTESA), il lockstep ha perso dati.

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
	$(CC) $(CFLAGS) -o $@ tests/test_broadcast.c src/risc.c src/grid.c src/elf.c

test-broadcast: build/test_broadcast asm/broadcast.s
	@$(AS) $(ASFLAGS) --defsym VALORE=$(VALORE) -o build/bc.o asm/broadcast.s || exit 1
	@for f in $(FORME); do \
	  printf 'broadcast,' >&2; \
	  ./build/test_broadcast build/bc.o $${f%x*} $${f#*x} $(VALORE) || exit 1; \
	done

# memtest.s si autoverifica e lascia il verdetto in s1, quindi gli basta
# l'harness di broadcast.

test-mem: build/test_broadcast asm/memtest.s
	@$(AS) $(ASFLAGS) -o build/memtest.o asm/memtest.s || exit 1
	@printf 'memtest,' >&2
	@./build/test_broadcast build/memtest.o 1 1 $$((0x11223344))

# Stesse forme del broadcast: 1x6 e 6x1 isolano i due estremi (catena di un solo
# hop su ogni colonna / colonna unica lunga), le altre li mescolano.
build/test_bordo: tests/test_bordo.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_bordo.c src/risc.c src/grid.c src/elf.c

test-bordo: build/test_bordo asm/bordo.s
	@$(AS) $(ASFLAGS) -o build/bordo.o asm/bordo.s || exit 1
	@for f in $(FORME); do \
	  printf 'bordo,' >&2; \
	  ./build/test_bordo build/bordo.o $${f%x*} $${f#*x} $(VALORE) || exit 1; \
	done

# Il totale atteso lo ricava il test dalla forma, quindi qui bastano le forme.
build/test_reduce: tests/test_reduce.c $(SRC) src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_reduce.c src/risc.c src/grid.c src/elf.c

test-reduce: build/test_reduce asm/reduce.s
	@$(AS) $(ASFLAGS) -o build/reduce.o asm/reduce.s || exit 1
	@for f in $(FORME); do \
	  printf 'reduce,' >&2; \
	  ./build/test_reduce build/reduce.o $${f%x*} $${f#*x} || exit 1; \
	done

# --- stencil ---------------------------------------------------------------
# Jacobi: ITER va scelto sulla tolleranza, e la tabella che il test stampa dice
# a quale iterazione il campo raggiunge il punto fisso. 64 basta a far
# convergere tutte le FORME con BORDO=64, così scatta anche l'asserzione che
# il punto fisso è la soluzione vera (e non quella troncata).
# SEMI: entrambi i campi iniziali previsti — 0 interno freddo, 1 campo r+c.
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
# La griglia RxC è la forma del risultato
# K è la lunghezza del prodotto interno e di fatto l'unico parametro,
# perché i dati arrivano dal bordo e non dalla .data.

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
# pesante.s non calcola niente di utile: PESO regola quante istruzioni di
# calcolo stanno fra due comunicazioni, ed è la variabile con cui si misura
# dove cade il punto di pareggio della parallelizzazione (si veda 'scala').
# Qui basta che il risultato sia esatto, quindi ITER e PESO restano bassi:
# lo sweep vero è in 'scala'.

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

# Le misure per la relazione.
# I test stampano già una riga CSV per run su stderr, qui si aggiunge solo
# l'intestazione e si raccoglie. CSV=1 zittisce le stampe multiriga
# (mappa dell'attesa del broadcast, traccia dei delta di Jacobi) che
# spezzerebbero il formato. Colonne documentate in docs/dati/README.md.
#   make dati                       rigenera tutto
#   column -t -s, docs/dati/*.csv   per leggerle a occhio
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
# CSV a parte: qui la riga per run non la stampa l'harness ma il cronometro in
# src/grid.c, acceso da TEMPI. Stesso sweep di make dati, ripetuto per ogni
# numero di thread, quindi ogni kernel compare una volta per ogni valore di
# THREAD e il confronto parallelo/seriale e' il rapporto fra righe omologhe.
#   make tempi                  sweep completo su THREAD
#   make tempi THREAD='1 8'     solo due punti
# I CSV di make dati NON devono cambiare fra un thread e l'altro: se cambiano
# e' il determinismo rotto, non una misura lenta.
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
# 'tempi' misura i kernel veri sulle FORME della suite, che si fermano a 12x12:
# lì la griglia è troppo piccola perché il parallelo paghi, e infatti perde.
# Questo sweep risponde a una domanda diversa: DOVE cade il pareggio, e come si
# sposta al crescere del lavoro per cella. Per questo usa un solo kernel,
# pesante.s, facendo variare PESO: è l'unica variabile che cambia il rapporto
# fra calcolo e comunicazione a parità di tutto il resto.
#   make scala                     sweep completo (qualche minuto)
#   make scala SCALA_PESO=32       una sola curva
# Ogni punto è ripetuto SCALA_REP volte e in analisi si prende il MINIMO: su una
# macchina non dedicata il rumore può solo aggiungere tempo, mai toglierlo.
# I cicli sono interlacciati apposta, con la ripetizione più esterna e PESO più
# interno: eseguendo i PESO in blocchi separati, come si era fatto la prima
# volta, ogni blocco cade in un momento diverso e una deriva della macchina si
# traveste da effetto di PESO. Così invece la deriva colpisce tutte le
# condizioni allo stesso modo e resta confrontabile ciò che va confrontato.
.PHONY: scala
# 40x40 e 56x56 non sono di riempimento: il pareggio cade fra 32 e 48, e senza
# forme intermedie resta localizzato a un intervallo largo mezza ottava.
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

# Le coordinate del grafico di tesi vengono dal CSV, non trascritte a mano:
# così i numeri del grafico non possono divergere dai dati.
.PHONY: grafici
grafici:
	@python3 $(DATI)/grafici.py

# --- il costo della sola località -------------------------------------------
# Quanto costa simulare UNA istruzione al crescere della griglia, a un solo
# thread: lì OpenMP non entra in gioco, quindi ciò che resta è il passo di
# 16 KB fra celle contigue, che disperde lo stato caldo su una pagina per cella.
# Il costo per cella-ciclo è t_step / (cicli * R * C), calcolato in analisi.
#
# ITER è accoppiato alla forma perché le righe siano confrontabili fra loro:
# senza, una griglia piccola durerebbe millisecondi e una grande secondi, e il
# confronto misurerebbe anche l'avviamento. I valori qui sotto puntano a due
# secondi per run; se si cambia PESO vanno rifatti.
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

# --- l'intensità di calcolo a parità di cicli -------------------------------
# 'scala' fa variare PESO tenendo fisso ITER, quindi le curve non eseguono lo
# stesso numero di cicli e un residuo di costo di creazione dei thread resta
# nel confronto. Qui i cicli sono pari per costruzione: PESO=1 con ITER=62 e
# PESO=32 con ITER=8 fanno entrambi 2624 cicli (verificato, il target lo
# riasserisce sotto), pur differendo di trentadue volte nelle istruzioni di
# calcolo per comunicazione. Se lo speedup coincide, l'intensità di calcolo non
# si vede: è il controllo del risultato negativo di sezione 5.7.3 della tesi.
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
