# Teoria del progetto — dal livello canale ai programmi di prova (Fase 1-6)

> Cosa c'è nel repository oggi, perché ogni file esiste, e perché il livello
> canale è fatto così com'è. Documento auto-contenuto: ogni scelta è motivata
> da ragionamento, non da un riferimento esterno.

---

## 1. Panoramica

Il progetto parte da **stella**, un interprete (ISS) RISC_V a 32 bit già
funzionante: legge un eseguibile ELF, lo carica in memoria ed esegue un ciclo
fetch → decode → execute. La Fase 0 ha messo ordine attorno a questo nucleo
(repo git, struttura a cartelle, build automatizzata). La Fase 1 ha aggiunto
il primo mattone nuovo: un **contratto per lo scambio di dati tra RISC**, il
canale.

Struttura attuale:
```
riscv/
  src/     risc.c risc.h elf.c elf.h main.c channel.h grid.c grid.h
  asm/     macros.s prodcons.s chain.s broadcast.s memtest.s bordo.s reduce.s jacobi.s
  tests/   test_channel.c test_catena.c test_broadcast.c test_bordo.c
           test_reduce.c test_jacobi.c
  docs/    (questo file, e altre note)
  Makefile
  .gitignore
```

---

## 2. I file sorgente

### [src/risc.h](../src/risc.h) e [src/risc.c](../src/risc.c)
Il cuore dell'interprete. `risc.h` definisce lo stato di un RISC
(`RISC_V`: registri `x0..x31`, program counter, memoria, più 4+4 registri
di bordo `in_reg`/`out_reg` ereditati dalla versione originale) e la struct
`DecodedInstr` che rappresenta un'istruzione già spacchettata nei suoi campi
(opcode, rd, rs1, rs2, funct3, funct7, immediato). `risc.c` implementa le tre
funzioni che fanno girare tutto: `fetch` (legge la parola in memoria
all'indirizzo `pc`), `decode` (la spacchetta), `execute` (uno `switch`
sull'opcode che applica l'effetto). Non ancora toccato dalla Fase 1: il case
`0x0B` (istruzioni custom) gestisce solo `IN`/`OUT` con copia diretta su
`in_reg`/`out_reg`, non passa ancora dal canale.

### [src/elf.h](../src/elf.h) e src/elf.c
Loader: legge un file ELF32 da disco, ne valida l'header, copia le sezioni
`.text`/`.data` nella memoria del RISC. Non modificato, non serve toccarlo per
il livello canale.

### [src/main.c](../src/main.c)
Driver da riga di comando: parsa gli argomenti, carica l'ELF, e a seconda del
numero di RISC richiesto lancia `run_single_risc` (esecuzione passo-passo
interattiva) o `run_multi_core` (una catena lineare di RISC collegati
Est-Ovest). Anche questo è ancora la versione originale: la propagazione tra
RISC è una copia diretta di `out_reg` nell'`in_reg` del vicino, senza alcun
controllo su "il dato è stato letto?" — è esattamente il problema che il
livello canale risolve.

### [src/channel.h](../src/channel.h)
Il contratto nuovo della Fase 1. Sezione dedicata più sotto.

### [asm/macros.s](../asm/macros.s)
Definisce in assembly le quattro istruzioni custom come macro `.insn`
(formato I-type, opcode `0x0B`, `funct3` 0-3 per IN/OUT/ISRDY/SETRDY). Serve
perché l'assembler GNU non conosce queste istruzioni: `.insn` permette di
scriverne l'encoding a mano senza patchare il toolchain. Un file `.s` che
vuole usarle farà `.include "macros.s"` e poi userà `IN`, `OUT`, `ISRDY`,
`SETRDY` come se fossero istruzioni vere.

---

## 3. Il sistema di build ([Makefile](../Makefile))

Target:
- `make` / `make neso` → compila tutti i `.c` in `src/` e li linka in
  `build/neso`.
- `make asm` → assembla ogni `.s` in `asm/` nel corrispondente `.o` in
  `build/`, con il toolchain `riscv64-unknown-elf-as` e i flag che fissano il
  target a RV32 (`-march=rv32i -mabi=ilp32 -Iasm` — il pacchetto si chiama
  "64" ma genera anche codice a 32 bit, è solo un nome).
- `make run` / `make step` → assembla *e lancia* un programma sulla griglia,
  nelle due modalità di esecuzione (sezione 9.5). Accetta `BORDO=n` per
  alimentare il perimetro con una costante (sezione 11.4).
- `make test` → il test del canale (sezione 4) più le verifiche end-to-end dei
  programmi assembly (`test-mem`, `test-prodcons`, `test-chain`,
  `test-broadcast`, `test-bordo`, `test-reduce`, `test-jacobi`; sezioni 9.4,
  9.6, 9.7, 11 e 12). Accetta `Q=n` per la mole di dati in transito, e
  `ITER=n` / `VALORE_BORDO=n` / `SEMI="0 1"` per lo stencil (sezione 12).
- `make clean` → cancella `build/`.

Sui flag dell'assembler, tre scelte non ovvie:
- **`-Iasm`**: `as` cerca i file di `.include` nella directory da cui viene
  lanciato (la root, dove gira `make`) e in quelle passate con `-I`, **non**
  nella directory del sorgente. Senza, un `.s` dentro `asm/` non trova
  `macros.s` che gli sta accanto.
- **niente `c` nel `-march`**: le istruzioni compresse sono lunghe 16 bit,
  mentre `fetch()` assume parole da 4 byte e avanza `pc += 4` sempre. Una
  sola istruzione compressa disallineerebbe tutto il resto del programma.
- **niente `_zicsr`**: l'interprete non implementa nessuna istruzione CSR, e
  dalla Fase 4 un opcode non implementato ferma il RISC invece di essere
  ignorato (sezione 9.2). Meglio che sia l'assembler a rifiutarle prima.

Due dettagli di Makefile che vale la pena capire:
- **`| build`** nelle regole (es. `build/neso: $(OBJ) | build`) è un
  *order-only prerequisite*: dice "assicurati che `build/` esista prima di
  eseguire questa regola" senza però far ricompilare tutto ogni volta che la
  cartella cambia timestamp (cosa che succederebbe con una dipendenza
  normale). `build:` esegue semplicemente `mkdir -p build`.
- **`build/%.o: src/%.c`** è una *pattern rule*: una sola riga sostituisce N
  regole esplicite, una per ogni sorgente. `make` la applica a ogni `.o`
  richiesto in `$(OBJ)`.

`build/` non è tracciata in git (vedi `.gitignore`): è output derivato, si
rigenera sempre da `make`, non ha senso versionarla.

---

## 4. Il test ([tests/test_channel.c](../tests/test_channel.c))

Filosofia: niente framework, un solo file con `assert()` da `<assert.h>` e un
`main()`. Se qualcosa si rompe, il programma va in abort con riga e
condizione esatta che ha fallito — sufficiente per un contratto di poche
funzioni, non serve altro.

Copre un ciclo produttore-consumatore completo:
1. stato iniziale vuoto/scrivibile;
2. pubblicazione che riesce (slot libero);
3. seconda pubblicazione che **deve fallire** (slot ancora pieno — è la
   garanzia anti-sovrascrittura);
4. lettura che consuma e libera lo slot;
5. lettura su canale vuoto che non deve corrompere lo stato.

Due casi aggiunti in Fase 4, entrambi nati da un bug vero e non
dall'immaginazione (sezione 9.3):

6. `SETRDY` **senza una `OUT` accettata prima** deve tornare 0: non deve
   ripubblicare il dato vecchio spacciandolo per nuovo;
7. il giro completo della backpressure — `OUT` rifiutata perché lo slot è
   occupato, il consumatore che nello stesso ciclo svuota, e la `SETRDY` del
   ciclo dopo che **deve comunque fallire** perché quel dato non era mai
   entrato. È il caso che distingue "canale libero adesso" da "ho un dato
   nuovo da pubblicare": due cose diverse che la prima versione confondeva.

Si lancia con `make test`.

### Il secondo livello: [tests/test_catena.c](../tests/test_catena.c)
Il test qui sopra verifica il *contratto* del canale chiamandone le funzioni a
mano. Non dice niente su cosa succede quando a usarle è un programma vero, su
una griglia vera, con i tempi veri — ed è esattamente lì che il bug della
sezione 9.3 si nascondeva. `test_catena.c` copre quel livello: carica un `.o`
in una griglia 1×C, la fa girare fino a terminazione e **legge i registri
finali direttamente**, senza passare dalla stampa (nessun formato di output da
tenere allineato). Un solo binario serve sia `prodcons.s` sia `chain.s`, perché
il primo è il caso `C=2` del secondo. Oltre al risultato numerico controlla che
tutti i RISC si siano fermati per `ECALL` e non per tetto di cicli — cioè che
non ci sia stato un deadlock — e che nessuno si sia fermato a metà del proprio
compito.

---

## 5. Come funzionano i campi funct e i codici delle istruzioni

Ogni istruzione RISC_V è una parola di 32 bit. Il modo in cui quei bit si
dividono in campi dipende dal **formato** dell'istruzione; quello che ci
interessa qui è l'**I-type**, lo stesso di `ADDI` e delle nostre quattro
istruzioni custom:

```
| imm[11:0] |  rs1  | funct3 |  rd  | opcode |
|  31..20   | 19..15| 14..12 | 11..7|  6..0  |
```

- **`opcode` (7 bit, 128 valori)** — il campo che lo `switch` principale di
  `execute()` guarda per primo. Seleziona la *famiglia*: "questa è
  un'operazione immediata", "questo è un salto", "questa è un'istruzione
  custom", ecc. Non basta da solo a identificare una singola istruzione: ci
  sono molte più istruzioni che famiglie.
- **`funct3` (3 bit, 8 valori)** — un secondo livello di selezione *dentro*
  la famiglia già scelta dall'opcode. È il campo che nel nostro `case 0x0B`
  distingue `IN` (0x0) da `OUT` (0x1) da `ISRDY` (0x2) da `SETRDY` (0x3): un
  solo opcode, quattro istruzioni diverse, un `if`/`switch` su `funct3` in
  più a livello di interprete.
- **`funct7` (7 bit, solo nel formato R-type)** — un terzo livello, usato
  quando anche opcode+funct3 non bastano a distinguere due istruzioni.
  Esempio classico (non nostro, ma è così che si legge ovunque): `ADD` e
  `SUB` condividono lo stesso opcode (`OP`, `0x33`) *e* lo stesso `funct3`
  (`0x0`) — a distinguerli è solo `funct7` (`0x00` vs `0x20`). Le nostre
  istruzioni custom sono I-type, quindi non hanno `funct7`: bastano
  opcode+funct3 perché sono solo 4.
- **`rd`, `rs1`, `imm`** — gli operandi: registro destinazione, registro
  sorgente, immediato con segno. Nel nostro caso `rs1` non è quasi mai usato
  (solo `OUT` lo legge, per sapere cosa spedire) e il valore `dir` (0..3) è
  incastrato nei 2 bit bassi dell'immediato invece di occupare un intero
  campo registro — non ci serve un registro per un numero che sta in 2 bit.

### Perché l'opcode `0x0B`
RISC_V lascia alcuni valori di opcode (i cosiddetti "custom-0/1/2/3")
volutamente non assegnati a nessuna istruzione standard, apposta perché
chiunque estenda l'ISA possa usarli senza mai collidere con un'istruzione
esistente o futura. `0x0B` (`0b0001011`) è uno di questi: usarlo garantisce
che il nostro interprete non confonda mai `IN`/`OUT`/`ISRDY`/`SETRDY` con
un'istruzione RV32I vera.

### Leggere/costruire un encoding a mano
Esempio: `OUT a0, EST` con `EST=1` (mappatura di `macros.s`), `a0` = `x10`.

1. `opcode` = `0x0B` = `0b0001011`
2. `rd` = 0 (OUT non scrive nulla, per convenzione si mette `x0`)
3. `funct3` = `0x1` (OUT)
4. `rs1` = 10 (`a0`) = `0b01010`
5. `imm[11:0]` = `1` (EST) = `0b000000000001`

Si impacchettano da sinistra a destra secondo lo schema sopra e si legge il
risultato in esadecimale — è esattamente il procedimento che l'assembler fa
per noi quando scriviamo `.insn i 0x0B, 0x1, x0, \rs, \dir` in
[macros.s](../asm/macros.s), e che si può verificare disassemblando l'output
con `objdump -d`.

---

## 6. Livello canale — `channel.h` in dettaglio

### La struct — doppia bufferizzazione: attuale + successivo
```c
typedef struct Channel {
    uint32_t data;        // stato ATTUALE: quello che tutti leggono per l'intero ciclo
    uint8_t  wp, rp;

    uint32_t data_next;   // registro di uscita: dove vanno tutte le scritture del ciclo
    uint8_t  wp_next, rp_next;
} Channel;
```
Versione rivista in Fase 3 (la prima versione, a 3 campi, è rimasta valida
per tutta la Fase 1-2 — vedi il problema che ha reso necessario aggiungere
`_next`, sezione 8, "il problema del two-phase"). Indicazione del relatore:
lo stesso schema a doppio buffer che la tesi usava solo per i dati va esteso
anche ai bit di controllo — uniforme, non due meccanismi diversi.

In Fase 4 la struct ha avuto per un periodo un sesto campo, `pending`, che
ricordava fra `OUT` e `SETRDY` se una scrittura era stata accettata (sezione
9.3). È stato **rimosso in revisione**: il canale deve essere un sistema a
transizione di livello con due contatori mod 2 e i loro comparatori, e nulla
oltre. Il problema che `pending` risolveva è reale e resta; a coprirlo ora è la
**cattura del dato sul fronte di `wp`** (`ch_commit`, più avanti in questa
sezione), che non è stato aggiuntivo ma il segnale di pubblicazione stesso.

I due `data` non sono quindi due buffer generici ma **due registri in serie**:
`data_next` è il registro di uscita del mittente — privato, il consumatore non
lo vede mai — e `data` è quello visibile, il cui enable è la transizione di
`wp`. Da qui discende tutto il resto del contratto.

**Perché non un semplice `bool full`?** Stesso motivo di prima: `wp`/`rp`
separati fanno sì che produttore e consumatore non scrivano mai la stessa
variabile — niente interferenze anche se il canale diventasse concorrente.

Stato derivato dal confronto dei due contatori (sempre quelli **attuali**):
```
wp == rp  →  vuoto / scrivibile
wp != rp  →  pieno / leggibile
```

### `ch_isrdy` / `ch_iswrt` — letture, sempre dallo stato attuale
```c
static inline int ch_isrdy(const Channel *c) { return c->wp != c->rp; }
static inline int ch_iswrt(const Channel *c) { return c->wp == c->rp; }
```
Non cambiate dalla versione precedente: leggono `wp`/`rp` (mai `_next`), quindi
restano congelate per tutto il ciclo, a prescindere da quante scritture
`_next` avvengano nel frattempo. `static inline` per lo stesso motivo di
sempre: header incluso da più `.c`, `static` evita conflitti al link,
`inline` fa sparire la chiamata per funzioni di una riga.

### `ch_setrdy` — pubblicazione del produttore, differita
```c
static inline int ch_setrdy(Channel *c) {
    if (ch_iswrt(c) && c->wp_next == c->wp) {
        c->wp_next = c->wp ^ 1;
        return 1;
    }
    return 0;
}
```
Stessa backpressure di sempre (controlla lo stato attuale prima di accettare
la pubblicazione, rifiuta se pieno), ma ora **non tocca più `wp` — scrive
`wp_next`**. È l'**unica** delle operazioni del canale che possa essere
rifiutata, ed è il comparatore a rifiutarla: `SETRDY` pubblica il contenuto
corrente del registro di uscita, qualunque esso sia. Se il programma non l'ha
caricato con una `OUT`, ripubblica il valore vecchio — e il canale non ha modo
di accorgersene, perché accorgersene richiederebbe ricordare che una `OUT` è
passata di lì. Quello era `pending`, e non c'è più: il contratto è che caricare
il registro è compito della `OUT`.
L'effetto non è visibile a nessuno (nemmeno a una `ch_isrdy`
chiamata subito dopo, nello stesso ciclo) finché non arriva `ch_commit`. È
esattamente la proprietà che serviva: un `SETRDY` di un RISC non deve mai
essere visibile a un `ISRDY` di un altro RISC nello stesso ciclo, altrimenti
il risultato dipenderebbe dall'ordine di scansione (sezione 8).

### `ch_read_c` — lettura del consumatore, differita
```c
static inline uint32_t ch_read_c(Channel *c) {
    uint32_t v = c->data;
    if (ch_isrdy(c)) c->rp_next = c->rp ^ 1;
    return v;
}
```
Legge sempre `data` (attuale, mai `data_next` — chi chiama vede lo stato
congelato a inizio ciclo), ma il consumo (`rp`) va nel `_next` come per
`SETRDY`: simmetria voluta, stesso meccanismo su entrambi i lati del
protocollo. Il resto del ragionamento (perché non segnala errore a canale
vuoto, perché `rp_next` non si muove se non c'era nulla da leggere) è
identico a prima — vale sull'attuale/next esattamente come valeva su un
singolo stato.

### `ch_write` — il pezzo che mancava
```c
static inline void ch_write(Channel *c, uint32_t v) {
    if (c->wp_next == c->wp) {
        c->data_next = v;
    }
}
```
Prima non esisteva (`OUT` scriveva `c->data` a mano, direttamente da
`risc.c`) — la sezione 6 originale lo segnalava come "punto aperto". Ora
serve per forza: `OUT` deve scrivere nello stato *successivo* come tutto il
resto, e solo una funzione del contratto può saperlo fare correttamente.

**La `OUT` non viene mai rifiutata**, ed è la differenza più importante rispetto
alla versione con `pending`. Lì la guardia era `ch_iswrt(c) && ...`: una `OUT` a
canale pieno non entrava, per non sovrascrivere un dato pubblicato e mai letto.
Ma quel rifiuto era proprio ciò che creava il buco della sezione 9.3 — se al
ciclo dopo il consumatore aveva svuotato, la `SETRDY` riusciva e pubblicava il
contenuto vecchio.

Togliendo la guardia il buco si chiude da sé: il valore nuovo è già nel registro
di uscita quando la `SETRDY` riesce. E il dato pubblicato non rischia niente,
perché il consumatore legge `data`, non `data_next`, e `data` si aggiorna solo
sul fronte di `wp` (`ch_commit`, più sotto). È lo stesso "punto su cui non
transigere #1" dello spec, ottenuto spostando il gate invece di aggiungere
memoria: un `.s` scritto male può sprecare cicli, non può perdere un dato.
`risc.c` non tocca più `Channel` direttamente in nessun punto — passa sempre
da `channel.h` (sezione 7).

L'unica guardia rimasta, `c->wp_next == c->wp`, è arrivata in Fase 5 e risponde
a una domanda diversa dalla backpressure: non "il canale era libero a inizio
ciclo?" ma "non l'ho già impegnato *dentro* questo ciclo?". Il confronto
risponde gratis, senza un bit nuovo:
`ch_commit` copia `wp_next` in `wp`, quindi dopo ogni commit i due sono
identici per costruzione, e `wp_next != wp` significa una cosa sola — c'è già
una pubblicazione in volo in questo ciclo. Senza il confronto, due
`ch_write`+`ch_setrdy` consecutive nello stesso ciclo non corrompevano lo
stato (la seconda `ch_setrdy` ricalcola `wp_next = wp ^ 1` a partire da `wp`,
non da `wp_next`, quindi il bit non tornava indietro) ma **sostituivano il
dato, perdendo il primo in silenzio**: il valore più insidioso di tutti,
quello che sparisce senza che nessuno segnali un errore.

Un RISC non può arrivarci: esegue una istruzione per ciclo, quindi due `OUT`
sono per forza in cicli diversi. È l'**host** che può, perché non è vincolato
dal fetch — e con l'I/O di bordo (sezione 11) l'host è diventato un produttore
vero sui canali di perimetro. Vale la pena notare dove sta la correzione: non
nell'host che deve ricordarsi di non chiamare due volte, ma nella funzione
condivisa da cui passano *tutti* i produttori. Un chiamante che spinge due
volte ora riceve semplicemente `0` dalla seconda `ch_setrdy`, cioè lo stesso
"rifiutata, ritenta" che riceverebbe da un canale pieno: la backpressure che
già conosce, invece di un caso speciale da studiare.

### `ch_commit` — applica tutto in blocco
```c
static inline void ch_commit(Channel *c) {
    if (c->wp_next != c->wp) {      // enable = il segnale di pubblicazione
        c->data = c->data_next;
    }
    c->wp   = c->wp_next;
    c->rp   = c->rp_next;
}
```
I due contatori passano sempre; **il dato solo sulla transizione di `wp`**. La
condizione non è un caso particolare da ricordare: è l'enable del registro
visibile, pilotato dal fronte del contatore, ed è ciò che ha reso superfluo
`pending`. Senza, la `OUT` senza guardia di poco fa cancellerebbe al primo
commit un valore pubblicato e non ancora letto.

Nota implicita: `_next` "riparte" già uguale all'attuale da solo,
senza bisogno di un passo di reset a inizio ciclo — perché `ch_commit` è una
*copia*, non uno scambio: se in un ciclo nessuno tocca un canale, `_next` è
rimasto esattamente uguale a `data`/`wp`/`rp` dal commit del ciclo prima.

---

## 7. Fase 2 — nuove funzioni nell'interprete

### `risc.h`: addio ai registri piatti
`RISC_V` non ha più `in_reg[4]`/`out_reg[4]` (due semplici `uint32_t`,
niente stato di pronto). Ora ha:
```c
Channel  out_ch[4];   // canali che QUESTO RISC possiede e pubblica
Channel *in_ch[4];    // puntatori ai canali dei vicini (li collega la griglia)
```
`out_ch` per valore perché il RISC è proprietario di ciò che scrive;
`in_ch` per puntatore perché il mio "IN da una direzione" **è**, letteralmente,
l'`out_ch` del vicino in quella direzione — un puntatore condiviso invece di
una copia, niente da tenere sincronizzato a mano. Finché nessuno collega i
puntatori (compito del livello griglia, sezione 8), ogni `in_ch[d]` resta
`NULL`: lo garantisce `init_risc`, che lo azzera esplicitamente insieme al
resto dello stato del RISC.

### `risc.c`, `case 0x0B`: le quattro istruzioni vere
Prima c'erano solo `IN`/`OUT` con copia diretta su `in_reg`/`out_reg`. Ora
tutte e quattro operano sul canale:
```c
Channel *in = risc->in_ch[dir];                                  // NULL fuori dalla griglia

case 0x0: risc->regs[d.rd] = in ? ch_read_c(in) : 0;             // IN: legge e CONSUMA (differito)
case 0x1: ch_write(&risc->out_ch[dir], risc->regs[d.rs1]);       // OUT: solo il dato (differito)
case 0x2: risc->regs[d.rd] = in ? ch_isrdy(in) : 0;              // ISRDY
case 0x3: risc->regs[d.rd] = ch_setrdy(&risc->out_ch[dir]);      // SETRDY
```
Simmetria voluta: `IN`/`ISRDY` leggono sempre `in_ch[dir]` (il canale in
arrivo), `OUT`/`SETRDY` scrivono sempre `out_ch[dir]` (il canale in uscita).
Nessuna delle quattro tocca più direttamente un campo di `Channel` — passano
tutte da `channel.h`, che ora decide da sola cosa è immediato (le letture) e
cosa va differito al commit (le scritture, sezione 6). Ognuna traccia se
stessa con un `printf`, come già facevano tutte le altre istruzioni del file
(coerenza per il modo single-step).

Questo è anche l'**hook verso il livello array**: non è stato un passo a
parte, è una conseguenza diretta del cambio di `risc.h` — le istruzioni
operano già sul `Channel` del nodo, non su registri locali.

La guardia `in ? ... : 0` è arrivata in Fase 5, ed è un bug che era rimasto
nascosto per una ragione istruttiva: la sezione più sotto raccontava che
`print_state` aveva bisogno del controllo `risc->in_ch[d] ? ... : 0` "perché
finché la griglia non ha collegato i vicini, `in_ch[d]` è `NULL` e
dereferenziarlo senza controllo va in crash". Il ragionamento era giusto e la
conclusione applicata solo a metà: `print_state` era stato messo in sicurezza,
`case 0x0B` no. In griglia non si vedeva, perché dalla Fase 3 `grid_init`
cabla *anche* i perimetri sui canali di bordo e nessun `in_ch` resta `NULL`;
si vedeva solo nella modalità a singolo RISC (`./neso <file.elf>`, senza
righe e colonne), dove `init_risc` è l'unico a toccare i puntatori e li lascia
tutti azzerati. Qualunque `.s` con un `IN` o un `ISRDY` eseguito lì dentro
faceva segfault — cioè proprio la modalità che esiste per fare il debug **a
grana fine prima** di portare un kernel sulla griglia.

La semantica scelta per un ingresso scollegato non è un ripiego per non
crashare, è la descrizione onesta di quello che quel canale è: un bordo aperto
da cui non arriva niente, quindi un canale eternamente vuoto — `ISRDY` dà `0`,
`IN` dà `0`. Un programma scritto per la griglia si comporta a singolo RISC
come si comporterebbe su una griglia i cui vicini tacciono per sempre, che è
esattamente l'informazione che serve quando si sta isolando un bug.

### `case LOAD`/`case STORE`: la RAM del RISC è a byte
Fase 5, ed è la voce che la sezione 9.8 teneva in sospeso. Il difetto era che
`execute()` usava `regs[rs1] + imm` come **indice di `uint32_t memory[]`**,
mentre tutto il resto del programma tratta la stessa RAM come byte:
`carica_elf_in_risc` fa `memcpy` su `(uint8_t *)risc->memory + sh_addr`, e
`fetch` divide per 4 apposta (`memory[pc / 4]`). Due viste incoerenti della
stessa area: `lbu t2, 3(t1)` leggeva il byte 12.

```c
static const int LS_WIDTH[8] = { 1, 2, 4, 0, 1, 2, 0, 0 };   // LB LH LW - LBU LHU - -

static uint8_t *mem_ptr(RISC_V *RISC, uint32_t addr, int width) {
    if (width == 0 || addr > (uint32_t)sizeof(risc->memory) - (uint32_t)width) {
        /* traccia l'indirizzo */ risc->running = false; return NULL;
    }
    return (uint8_t *)risc->memory + addr;
}
```

Tre difetti nelle stesse righe, quindi una riscrittura sola:

1. **Lo stride.** L'indirizzo torna a essere quello che dice di essere, e la
   `uint32_t memory[]` resta `uint32_t` solo per comodità del `fetch`.
2. **L'assenza di un controllo di ampiezza**, che è la ragione per cui non era
   davvero rimandabile. Uno `sw` con indirizzo oltre i 16 KB scriveva *fuori*
   dalla struct del RISC, e siccome `grid_init` alloca `RISC` come array
   piatto (`malloc(rows * cols * sizeof(RISC_V))`), la cella successiva è
   adiacente in memoria: **un RISC poteva corrompere in silenzio i registri o
   la memoria del vicino**, senza passare da un `Channel`. Cioè poteva violare
   l'unica proprietà su cui poggia tutta la sezione 10 — che fra due celle non
   esiste un byte di stato condiviso tranne il canale. Un bug di indirizzamento
   sarebbe stato rimandabile; una falla nell'isolamento fra processori no.
3. **Il cast a `int32_t *` su un indirizzo potenzialmente disallineato**, che è
   comportamento indefinito. Sostituito da `memcpy`: una riga di libreria
   standard, corretta sempre, e per lo store una sola `memcpy` con la larghezza
   presa dalla tabella copre `sb`/`sh`/`sw` insieme — little-endian, i byte
   bassi per primi, senza tre rami che dicono la stessa cosa.

Il confronto usa `sizeof(risc->memory)` e non `RAM_SIZE` (che vale 16384 ed è
lo stesso numero) per non duplicare la costante e non far dipendere `risc.c` da
`elf.h`: la dimensione la conosce già il tipo, chiederla a lui è l'unica
versione che non può divergere. Un accesso fuori RAM ferma il RISC con una
traccia, come già fa il `default` dello `switch` per un opcode ignoto — la
stessa scelta di sempre in questo interprete: fermarsi e dire dove, mai
proseguire su uno stato che non ha più senso.

### `print_state`/`init_risc` aggiornati di conseguenza
`print_state` ora stampa `out_ch[d].data` direttamente e `in_ch[d]->data`
con una guardia (`risc->in_ch[d] ? ... : 0`): necessaria perché finché la
griglia non ha collegato i vicini, `in_ch[d]` è `NULL` e dereferenziarlo
senza controllo va in crash. `init_risc` inizializza ogni `out_ch[i]` a
canale vuoto (`(Channel){0}`) e ogni `in_ch[i]` a `NULL`.

Limite noto di questa stampa, emerso usandola davvero in Fase 4: mostra solo
`data`, mai `wp`/`rp`. Siccome `ch_read_c` consuma spostando `rp` ma **non
azzera `data`**, un canale svuotato continua a esibire l'ultimo valore letto:
`EST=5` a fine esecuzione è un residuo, non un dato pendente. Per il debug dei
kernel 2D servirà affiancarci lo stato pieno/vuoto (`ch_isrdy`), altrimenti un
deadlock si distingue da una terminazione normale solo per congettura.

### `ECALL` e il caso `default`
Due aggiunte allo `switch` di `execute()`, entrambe pretese dalla Fase 4:
```c
case ECALL:  risc->running = false; break;               // 0x73: fine programma
default:     /* traccia opcode, pc, istruzione */
             risc->running = false; break;
```
`ECALL` è l'unico modo che un programma ha di **dichiarare** di aver finito:
prima l'unica terminazione possibile era il `pc` che sfondava la memoria, e
`grid_any_running` non poteva mai diventare falsa per scelta del programma.
Non serve toccare `decode()`: `ECALL` è una I-type con `rs1`, `funct3` e
immediato tutti a zero, quindi l'opcode da solo la identifica.

Il `default` è meno ovvio ma vale altrettanto: prima uno `switch` senza ramo
di scarto trattava ogni opcode non implementato come una NOP silenziosa — un
refuso, una pseudo-istruzione espansa in qualcosa di non supportato, o
semplicemente il codice che scappa oltre la sua ultima istruzione (la memoria
azzerata si decodifica come opcode `0x00`) proseguivano senza un rumore.
Adesso ognuno di questi ferma il RISC e stampa `pc` e istruzione. Nota sul
`pc` stampato: `fetch()` lo ha già incrementato, quindi va riportato
`pc - 4`, altrimenti indica l'istruzione successiva a quella colpevole.

Conseguenza da tenere presente scrivendo i `.s`: **ogni programma deve
chiudersi con `ecall`**, altrimenti finisce nel `default` e segnala un errore
che errore non è.

### `a0..a3` = identità cablata della cella
`grid_init` scrive in ogni cella, subito dopo `init_risc`:
```c
k->regs[10] = r;      // a0 = riga
k->regs[11] = c;      // a1 = colonna
k->regs[12] = rows;   // a2 = righe totali
k->regs[13] = cols;   // a3 = colonne totali
```
Serve perché `main.c` carica lo **stesso** ELF in tutte le celle: senza un modo
di sapere dove si è, ogni nodo può solo comportarsi come tutti gli altri, e
programmi come produttore/consumatore — dove una cella genera e un'altra
riceve — non sono scrivibili affatto.

**Perché le coordinate e non l'id lineare.** La prima versione passava solo
`a0 = r*cols + c`. Basta finché la domanda è "sono il nodo 0?", ma i programmi
veri chiedono altro: *sono il bordo ovest?* (`c == 0`), *sono l'ultima
colonna?* (`c == cols-1`). Ricavarle da un id lineare significa `id % cols` e
`id / cols`, cioè divisione e modulo: istruzioni dell'estensione **M**, che
`rv32i` non ha. Far calcolare a ogni cella una cosa che `grid_init` ha già in
mano dentro il proprio doppio ciclo sarebbe assurdo, e comprare un'estensione
dell'ISA per ottenerla ancora di più. Con le coordinate precaricate ogni test
di bordo è un confronto secco (`beqz a1`, `addi t0,a3,-1; beq a1,t0`).

Anche il modello fisico dice questo: in un array systolic vero le coordinate
della cella sono cablate, non calcolate a runtime. L'equivalente RISC_V
ufficiale sarebbe il CSR `mhartid` (`csrr a0, mhartid`), che è come i multicore
veri distinguono le proprie hart: darebbe la stessa informazione al costo di un
file CSR e dell'estensione `zicsr` nel decoder.

Perché va in `grid_init` e non in `init_risc`: `init_risc` inizializza *un
RISC*, e un RISC da solo non sa niente di righe e colonne — la topologia è
conoscenza del livello griglia. `main.c` in modalità singolo RISC chiama
`init_risc` direttamente e le celle restano a zero, che è corretto.

Funziona perché `grid_init` gira *prima* che `main` carichi l'ELF, e il loader
tocca solo `risc->memory`, mai i registri. Nei `.s` conviene copiare subito il
valore che serve (`mv s0, a1`) come si fa con `argc` in un programma normale:
`a0..a3` sono registri di scambio che la prima routine che passa sovrascrive.

---

## 8. Fase 3 — la griglia (stato attuale)

### `grid.h`/`grid.c`: la logica dell'array separata da `main.c`
Nuovi file dedicati (invece di infilare tutto in `main.c`, che tra l'altro
con la vecchia `run_multi_core` non compila più contro il nuovo `risc.h` —
va riscritta quando la griglia sarà pronta). Scelta di disegno: griglia
**RxC generale** fin da subito, non prima una catena lineare da generalizzare
poi — una catena a N nodi è solo una griglia 1×N, il codice di cablaggio dei
vicini è lo stesso in entrambi i casi.

```c
typedef struct {
    int rows, cols;
    RISC_V *risc;   // flat: rows*cols, cella (r,c) = risc[r*cols+c]
} Grid;
```
`RISC` è allocato con `malloc`, non un array a dimensione fissa: `rows`/
`cols` arriveranno da input a runtime (CLI), non sono noti a compile-time.

### `grid_init` — allocazione + cablaggio dei vicini
Due fasi in sequenza:
1. `init_risc` su ogni cella. Effetto collaterale voluto: azzera anche tutti
   gli `in_ch` a `NULL` per ogni cella — quindi le celle di bordo restano
   scollegate **senza scrivere codice apposta** per il caso bordo.
2. Cablaggio dei vicini interni, orizzontale (E-O) e verticale (N-S) in due
   cicli separati. Ogni coppia adiacente si collega con due righe simmetriche
   (regola "OUT del mittente = IN del ricevente" scritta nei due versi):
   ```c
   b->in_ch[OVEST] = &a->out_ch[EST];
   a->in_ch[EST]   = &b->out_ch[OVEST];
   ```
   Una sola iterazione per coppia basta per entrambi i versi di comunicazione,
   perché ogni direzione ha il proprio canale indipendente (non c'è un canale
   condiviso da "invertire").

### `grid_at`/`grid_free`
`grid_at` centralizza in un solo posto l'aritmetica dell'indicizzazione flat
(`r*cols+c`), così non si ripete in ogni file che tocca una cella.
`grid_free` libera sia `RISC` sia `border` (sotto) e azzera i puntatori
(idioma classico contro gli use-after-free).

### I canali di bordo
`in_ch` sulle celle di perimetro resta `NULL` dopo il cablaggio dei vicini
interni (nessun vicino da agganciare lì) — e senza correggerlo, un `ISRDY`/
`IN` eseguito in quella direzione dereferenzia `NULL` e crasha l'interprete.
`OUT`/`SETRDY` invece non crashano mai in quel caso: operano su `out_ch`, che
il RISC possiede per valore (mai un puntatore), quindi esiste sempre a
prescindere dal vicino.

`Grid` guadagna un campo per possedere i canali che chiudono quei buchi:
```c
Channel *border;   // 2*(rows+cols) canali, allocati e cablati in grid_init
```
`grid_init`, dopo i due cicli di cablaggio interno, fa una terza passata che
punta ogni `in_ch` ancora `NULL` (quelli di perimetro) a un `Channel` fresco
di `grid->border`. La formula `2*(rows+cols)` regge anche nei casi degeneri
(griglia 1×N, N×1, 1×1) senza casi speciali: NORD-aperti e SUD-aperti sono
sempre `cols` slot ciascuno, OVEST-aperti ed EST-aperti sempre `rows`
ciascuno, anche quando una cella è ai margini su più di un lato — sono slot
distinti sulla stessa cella, non si sovrappongono mai. L'host (`main.c`, in
futuro i kernel di prova) userà questi canali esattamente come userebbe un
vicino vero: stessa `ch_write`/`ch_setrdy`/`ch_isrdy` — coerente con la scelta
di lasciare `in_ch`/`out_ch` pubblici fin dall'inizio.

> **Rettifica (Fase 5).** La frase sopra prevedeva anche "nessuna funzione
> wrapper dedicata", e su quello si sbagliava. Il protocollo previsto era
> giusto — l'host usa davvero le stesse quattro funzioni di un vicino — ma
> chiamarle direttamente dal chiamante lasciava fuori due cose che *non* sono
> del protocollo e vanno decise una volta sola: che le coordinate puntino
> davvero fuori dalla griglia (altrimenti si inietterebbero dati su un canale
> interno, spacciandosi per un vicino vero), e che leggere un output di
> perimetro significhi **consumarlo** e non solo guardarlo. Da qui `grid_push`
> e `grid_pop`, sezione 11: tre righe di corpo ciascuna, che non aggiungono
> nulla al protocollo e chiudono la parte che il protocollo non copriva.

### `grid_step` — implementato: il two-phase risolto
```c
void grid_step(Grid *grid) {
    int n = grid->rows * grid->cols;

    for (int i = 0; i < n; i++)                    // fase compute
        if (grid->risc[i].running) execute_step(&grid->risc[i]);

    for (int i = 0; i < n; i++)                     // fase commit: i canali dei RISC
        for (int d = 0; d < 4; d++)
            ch_commit(&grid->risc[i].out_ch[d]);

    for (int i = 0; i < 2 * (grid->rows + grid->cols); i++)  // fase commit: il bordo
        ch_commit(&grid->border[i]);
}
```
Il problema che questa sezione segnalava come aperto (se il RISC A pubblica
con `SETRDY` e nello stesso ciclo il vicino B legge con `ISRDY`, il risultato
dipendeva dall'ordine di scansione) è risolto — non qui, ma spostando il
meccanismo dentro `channel.h` (sezione 6): `ch_setrdy`/`ch_read_c` scrivono
solo `_next`, mai lo stato attuale, quindi durante l'intera fase compute
ogni lettura in qualunque RISC vede lo stesso stato congelato a inizio
ciclo, a prescindere dall'ordine con cui `grid_step` scandisce i RISC.
`grid_step` stesso resta perciò semplicissimo: due cicli in sequenza
(compute, poi i due commit) — nessuna barriera vera necessaria, perché è a
singolo thread e due `for` consecutivi danno la stessa garanzia gratis
(confermato dal relatore: è lo stesso doppio buffer che la tesi usava solo
per i dati, qui esteso anche al bit di controllo).

### `main.c`: singolo RISC per debug, griglia per l'array
Due modalità, selezionate dal numero di argomenti sulla riga di comando —
stesso schema che usava già il `main` originale, solo pulito:
```
Debug singolo RISC: ./neso <file.elf>
Griglia:            ./neso <file.elf> <rows> <cols> <cicli>
```
`run_single_risc` (invariata) resta per il debug a **grana fine**: un
programma alla volta, un'istruzione alla volta, nessuna griglia in mezzo —
verifica che la logica sia corretta prima di scoprire se un bug è nel
programma o nell'interazione con i vicini. `run_grid` è il suo equivalente a
**grana di ciclo**: chiama `grid_step` e stampa lo stato (`IN:`/`OUT:` di
ogni cella, già disponibile da `print_state`) — la stessa UX interattiva che
aveva già la vecchia `run_multi_core` della tesi, portata sulla griglia
invece che sulla catena. Il ciclo si ferma al primo tra due
eventi: `cicli` raggiunti, oppure nessun RISC più `running`
(`grid_any_running`) — è il fix della terminazione del ROADMAP, nessun
contatore separato da tenere sincronizzato (il bug della tesi era proprio
un `core_attivi` mai decrementato).

`run_multi_core` (la catena E-W cablata a mano, coi vettori
`inOvest_vettore`/`inEst_vettore` precalcolati) è stata rimossa, non solo
sostituita: una catena a N nodi è già un caso particolare di griglia 1×N,
tenerla in parallelo sarebbe stato codice morto da manutenere per un caso
già coperto.

Testato end-to-end (griglia 2×2, `test_isa.elf`, `IN/OUT/ISRDY/SETRDY`):
un `SETRDY` eseguito al ciclo 1 da un RISC diventa visibile a `ISRDY` del
vicino solo al ciclo 2, mai prima — la latenza di un ciclo per hop è
osservata, non solo progettata. Nessun crash sulle celle di bordo mai
alimentate (`ISRDY` torna correttamente `0`).

### Fase 3 completa
Tutti i punti della scaletta chiusi: array separato dal main, topologia RxC
parametrica, canali interni con `opp(d)=d^2`, two-phase vero, registri di
bordo, input da CLI, terminazione senza contatore morto.

---

## 9. Fase 4 — i programmi di prova

La Fase 4 è il primo momento in cui il progetto viene usato invece che
costruito, ed è per questo che è anche il primo momento in cui salta fuori un
errore di progetto: le fasi 1-3 erano state verificate con test scritti da chi
aveva appena scritto il codice, cioè da chi condivideva gli stessi assunti.

### 9.1 Un solo binario, due comportamenti

`main.c` carica lo stesso ELF in ogni cella, quindi produttore e consumatore
vivono nello **stesso** file, separati da un salto sulla posizione:
```asm
_start:
    mv     s0, a1          # a1 = la mia colonna
    beqz   s0, produttore  # colonna 0 -> produce
    j      consumatore
```
È il modello "SPMD" (*single program, multiple data*): un unico programma,
comportamenti diversi in funzione dell'identità, che è come si programmano gli
array reali. L'alternativa — un `.s` per nodo — moltiplicherebbe i file per il
numero di celle e renderebbe impossibile far girare lo stesso kernel su
griglie di dimensione diversa.

Il ruolo si sceglie sulla **posizione** (`a1`, la colonna) e non su un id: è la
scelta discussa in sezione 7, e serve appena la catena si allunga (sezione
9.6), dove i ruoli sono "primo", "ultimo" e "in mezzo".

Lo scheletro del protocollo, che è poi lo stesso in tutti i kernel che
verranno:
```asm
1:  OUT    s1, EST         # produttore
    SETRDY t0, EST
    beqz   t0, 1b          # rifiutata -> ritenta OUT *e* SETRDY

4:  ISRDY  t0, OVEST       # consumatore
    beqz   t0, 4b          # niente da leggere -> respin
    IN     t1, OVEST
```
Due invarianti che valgono per ogni programma futuro: **`ISRDY` prima di ogni
`IN`** (altrimenti `ch_read_c` restituisce il dato vecchio senza segnalare
nulla), e **il ritentativo torna sulla `OUT`, non sulla `SETRDY`** — perché se
la `SETRDY` è fallita significa che la `OUT` era stata rifiutata, e va
rieseguita.

In [asm/prodcons.s](../asm/prodcons.s) i due rami di ritentativo sono scritti
per esteso (`bnez` a un'etichetta in avanti invece di `beqz` all'indietro) solo
per infilarci i contatori della sezione 9.5: la logica è quella qui sopra.

### 9.2 Terminazione

Vedi sezione 7 (`ECALL` e `default`). Il punto di teoria: prima della Fase 4
il simulatore non aveva alcuna nozione di "programma finito", solo di "cicli
esauriti". Con `ECALL` la terminazione diventa una proprietà del *programma*, e
`grid_any_running` — che esisteva già ma non poteva mai essere falsa — inizia
finalmente a fare il suo mestiere. Nella stampa finale si legge la differenza:
`tutti i RISC fermi` è una terminazione, `limite cicli raggiunto` è un
deadlock (o un `N` troppo basso).

### 9.3 Il bug che il primo programma ha scoperto

Il caso di prova più informativo non è il produttore/consumatore che funziona:
è quello con un **consumatore lento**, che tiene il canale pieno e costringe il
produttore ad aspettare. Somma attesa 1+2+3+4+5 = 15; il simulatore rispondeva
**19**. Il dato veniva sovrascritto prima di essere letto.

Primo tentativo di correzione: una guardia in `ch_write`, "una `OUT` su canale
pieno non entra". Il risultato è passato da 19 a **14**, cioè da un errore a un
altro. Lo scenario, ciclo per ciclo, con il canale pieno di v1 non ancora letto:

| ciclo | produttore | consumatore | dopo il commit |
|-------|-----------|-------------|----------------|
| N     | `OUT v2` → rifiutata dalla guardia | `IN` → legge v1, muove `rp_next` | canale **vuoto**, `data` = v1 |
| N+1   | `SETRDY` → trova vuoto → ritorna **1** | | v1 **ripubblicato** |

Il produttore crede di aver spedito v2 e passa oltre; il consumatore riceve v1
due volte; v2 non esiste più. La guardia aveva trasformato una sovrascrittura
silenziosa in un duplicato silenzioso.

La causa vera è che **`OUT` e `SETRDY` sono due istruzioni in due cicli
diversi**, e fra l'una e l'altra lo stato del canale cambia sotto i piedi.
`SETRDY` decideva guardando "il canale è libero?", che nel ciclo N+1 è una
domanda diversa da quella che le serviva: "ho un dato nuovo da pubblicare?".
Nel ciclo N erano la stessa domanda, nel ciclo N+1 no più.

**Prima risposta: `pending`.** Un bit che ricorda, da un'istruzione all'altra,
che la `OUT` è stata accettata — il *write-enable latch* del modello hardware.
Con `pending`, `SETRDY` in N+1 ritorna 0, il produttore ritenta la `OUT`, e il
dato arriva. La somma tornava a 15.

**Perché è stato scartato.** In revisione il relatore ha chiesto che il canale
sia un sistema a **transizione di livello**, con contatori mod 2 e comparatori e
nient'altro: `pending` è stato per l'appunto stato di protocollo in più.

**Risposta definitiva: togliere la guardia, non aggiungere il bit.** Il guaio
si legge nella tabella qui sopra, ed è la parola *rifiutata* nella riga N. La
`OUT` scrive `data_next`, che il consumatore non vede mai: rifiutarla non
protegge nessuno, e lascia il registro con il valore vecchio. Lasciandola
passare sempre, la stessa traccia diventa:

| ciclo | produttore | consumatore | dopo il commit |
|-------|-----------|-------------|----------------|
| N     | `OUT v2` → **entra** in `data_next` | `IN` → legge v1, muove `rp_next` | canale **vuoto**, `data` = v1 |
| N+1   | `SETRDY` → trova vuoto → ritorna **1** | | **v2** pubblicato, `data` = v2 |

E v1 non viene calpestato dalla `OUT` del ciclo N perché `data` si aggiorna
**solo sulla transizione di `wp`** (`ch_commit`, sezione 6): finché nessuno
pubblica, il valore visibile resta quello che è.

Nota che questa versione è anche **più veloce** di quella con `pending`: lì il
produttore era costretto a un giro di respin proprio in questo caso, qui passa
al primo colpo. Rigenerando tutti gli sweep dopo la rimozione, `catena.csv` è
sceso (`prodcons` a `RITARDO=0`: 49 → 45 cicli, 2 → 1 ritentativi) e gli altri
cinque file sono rimasti **identici byte per byte** — perché sono gli unici in
cui una `SETRDY` fra celle venga mai rifiutata.

**La lezione di metodo** vale più della correzione: un protocollo two-phase
non basta metterlo sui *dati*, va messo su tutto ciò che attraversa un confine
di ciclo — e un'operazione logicamente atomica ("spedisci") spezzata in due
istruzioni crea un confine di ciclo in mezzo, dove prima non c'era. La seconda
lezione è che davanti a un buco del genere la mossa giusta può essere
**spostare un gate invece di aggiungere memoria**: il primo istinto era
ricordare di più, la soluzione era controllare di meno.

### 9.4 Come si verifica che regga

Un solo numero non dimostra niente: il produttore/consumatore "giusto" dava 15
anche con il bug, purché i due nodi andassero alla stessa velocità. La verifica
sensata è far **variare la velocità relativa** e controllare che il risultato
non si muova. Il consumatore ha un ritardo artificiale parametrico:

```
make run P=prodcons R=1 C=2 N=2000 DEFS="--defsym RITARDO=25"
```

`--defsym` definisce un simbolo dalla riga di comando; nel `.s` un
`.ifndef RITARDO / .equ RITARDO, 0 / .endif` gli dà un default. Una sola
sorgente per tutte le varianti, invece di un file per configurazione.

Risultati con `RITARDO` ∈ {0, 1, 4, 8, 25, 60}: somma **15 sempre**. È questa
invarianza, non il valore singolo, la proprietà che lo spec chiama "punto su
cui non transigere #1".

### 9.5 Strumenti di misura e le due modalità di esecuzione

Il risultato finale dice *se* il protocollo ha funzionato, non *quanto* ha
dovuto lavorare. Due contatori nel programma lo rendono visibile:

- `s3` sul produttore — `SETRDY` rifiutate: quanto è stato messo in attesa;
- `s4` sul consumatore — `ISRDY` a vuoto: quanto è rimasto a digiuno.

Sono complementari e insieme dicono da che parte sta il collo di bottiglia:

| `RITARDO` | `s3` (produttore in attesa) | `s4` (consumatore a digiuno) |
|-----------|------------------------------|-------------------------------|
| 0         | 2                            | 3                             |
| 4         | 7                            | 0                             |
| 25        | 41                           | 0                             |

A ritardo zero i due nodi si aspettano a vicenda più o meno in parti uguali;
appena il consumatore rallenta la pressione si sposta tutta a monte e il
produttore passa il tempo a ritentare. È la firma della backpressure, misurata
invece che asserita — e su un array più grande sarà il modo per individuare
quale cella sta frenando le altre.

Le due modalità di esecuzione servono a due domande diverse:

```
make run  P=prodcons R=1 C=2 N=2000     # fila dritto, stampa solo lo stato finale
make step P=prodcons R=1 C=2 N=40       # un ciclo per INVIO, stato di ogni cella
```

`run` risponde a "il risultato è giusto?" ed è quella che si usa negli sweep,
dove l'interattività renderebbe impossibile lanciare venti configurazioni di
fila. `step` risponde a "in che ciclo esatto si è rotto?", ed è l'erede diretto
della UX della tesi. Sono lo stesso `run_grid`: la stampa per ciclo e l'attesa
dell'INVIO stanno dietro la variabile d'ambiente `STEP`, che `make step`
imposta. In entrambi i casi lo stato finale viene stampato sempre, insieme al
motivo della fermata.

### 9.6 `chain.s` — la catena 1×C

Secondo programma, e primo con più di due nodi: sorgente → inoltri → pozzo su
una riga di `C` celle. I tre ruoli si scelgono dalla posizione:

```asm
    mv     s0, a1          # la mia colonna
    addi   s5, a3, -1      # ultima colonna
    beqz   s0, sorgente    # c == 0
    beq    s0, s5, pozzo   # c == cols-1
    j      inoltro         # altrimenti
```

Il nodo di mezzo è `IN` da OVEST seguito da `OUT`+`SETRDY` verso EST, con
entrambi gli spin: aspetta che arrivi qualcosa **e** che il vicino a valle
liberi. Sa quando fermarsi contando `Q` passaggi — `Q` è una costante
di build nota a ogni cella, quindi non serve far viaggiare un valore sentinella
nel flusso dati.

**Perché resta separato da `prodcons.s`.** Con `C=2` non ci sono celle
intermedie e `chain.s` degenera esattamente in `prodcons.s`: uno è il caso
particolare dell'altro, e in un progetto di produzione ne terremmo uno solo.
Qui i due file hanno scopi diversi — `prodcons.s` è il protocollo nella sua
forma minima leggibile, due ruoli e nient'altro; `chain.s` è il caso generale.
Il **test** invece è uno solo (`tests/test_catena.c`, parametrico su `C`),
perché lì la duplicazione non aggiungerebbe niente da leggere.

**L'invariante diventa più forte.** Prima era "il risultato non dipende dalla
velocità relativa dei due nodi". Ora la stessa somma deve uscire anche
cambiando la lunghezza della catena: `make test` percorre `RITARDI × COLONNE`
= 6 × 4 configurazioni per `chain.s`, e la somma è 15 in tutte e 24.

Quello che cambia invece dice qualcosa:

| | C=2 | C=3 | C=5 | C=12 |
|---|---|---|---|---|
| cicli (`RITARDO`=0) | 50 | 58 | 74 | 130 |
| attese totali (`s4`) | 3 | 6 | 18 | 123 |
| ritentativi totali (`s3`) | 2 | 2 | 2 | 2 |

I cicli crescono linearmente con la catena — è la latenza di un hop per cella,
la stessa che la Fase 3 aveva osservato su 2×2. Le attese esplodono perché ogni
cella intermedia passa la vita a chiedere "c'è qualcosa?" mentre il dato è
ancora a monte: è il costo dello spin, non un difetto del protocollo. I
ritentativi restano invece piatti: a `RITARDO=0` nessuno frena da valle, e la
pressione all'indietro non si accumula lungo la catena. Alzando il ritardo del
pozzo si vede il contrario — i ritentativi salgono e le attese scendono, e la
cella con `s3` alto e `s4` basso è quella che sta aspettando il vicino a valle.
Su una griglia grande sarà così che si individua chi frena.

### 9.7 `broadcast.s` — il primo kernel 2D

Un valore parte da (0,0) e deve raggiungere **ogni** cella di una griglia R×C.
Ruoli dalla posizione, come sempre, ma con una scorciatoia: `r+c` è zero solo
per la cella d'angolo, quindi il test è `add t3, a0, a1; beqz t3, sorgente` —
una somma invece di due confronti. Ogni altra cella aspetta da NORD **o** da
OVEST (non sa quale dei due arriverà per primo) e ritrasmette a EST **e** SUD.

**La proprietà interessante: non contiene un solo test di bordo.** Me lo
aspettavo pieno di casi speciali sui margini, e invece i due problemi di bordo
si risolvono da soli, per due motivi diversi:

- *In ricezione*, la cella della riga 0 non ha un vicino a nord e il suo
  `in_ch[NORD]` punta a un canale di bordo che nessuno alimenta. `ISRDY`
  risponde 0 per sempre, il ciclo interroga anche OVEST e prosegue di lì.
  Il polling su entrambe le direzioni assorbe il caso "vicino mancante" senza
  un `if`. Ma regge **solo perché i canali di bordo esistono**: se `in_ch`
  fosse rimasto `NULL` come dopo il solo cablaggio dei vicini interni, quella
  `ISRDY` sarebbe un dereferenziamento di `NULL`. È la terza passata di
  `grid_init` (sezione 8) che qui si ripaga.
- *In trasmissione*, la cella dell'ultima colonna pubblica su un `out_ch[EST]`
  che non è l'ingresso di nessuno. Non blocca e non è un errore: il canale
  nasce vuoto, la `SETRDY` riesce al primo colpo, il dato resta lì come output
  scartato. Il fatto che `out_ch` sia posseduto *per valore* dal RISC (mentre
  `in_ch` è un puntatore) rende impossibile che un invio verso il vuoto
  crashi — asimmetria decisa in Fase 2 che paga adesso.

**Copertura e terminazione** si dimostrano per induzione su `r+c`: ogni cella
con `r+c > 0` ha almeno un predecessore (a nord o a ovest), che le manda il
valore; e ogni cella esegue esattamente una ricezione e due invii, quindi
termina. Dalla cella (1,1) in poi i predecessori sono **due** e mandano
entrambi: la cella ne consuma uno e lascia l'altro pubblicato e mai letto per
sempre. Non è un deadlock perché nessuno aspetta che quel canale si liberi —
si vede nella stampa finale come una manciata di canali "pieni" a fine corsa.

**Cosa non prova**: la backpressure. Ogni canale trasporta un solo valore in
tutta l'esecuzione, quindi ogni `SETRDY` riesce al primo tentativo e un
contatore `s3` sarebbe strutturalmente zero — per questo il programma non ce
l'ha. Quello che prova è il **fan-out** (due `OUT`+`SETRDY` su direzioni
diverse dalla stessa cella) e il cablaggio **verticale**, che nessun programma
aveva mai attraversato prima.

Il test [tests/test_broadcast.c](../tests/test_broadcast.c) asserisce su tutte
e R×C le celle — "raggiunge tutti" è la proprietà da dimostrare, verificarla in
un angolo non dimostrerebbe niente — e gira su `1x1`, `1x6`, `6x1`, `3x4`,
`4x4`. Le forme degeneri non sono una curiosità: `1x6` e `6x1` isolano i due
cicli di cablaggio di `grid_init`, quindi se fallisce solo una delle due si sa
subito quale; `1x1` è l'unica cella che è sorgente e foglia insieme, pubblica
su due canali che non legge nessuno e deve comunque terminare.

#### L'onda non è simmetrica

Su una 6×7 (104 cicli, tutte le celle a destinazione) i cicli di attesa `s4`
lungo i due bordi sono:

```
riga 0     (verso est):  0  0  1  2  3  4  5      +1 per hop
colonna 0  (verso sud):  0  1  3  5  7  9         +2 per hop
```

**Est corre il doppio di sud**, e infatti sull'antidiagonale `r+c=3` i valori
sono 2, 3, 4, 5 invece di essere uguali. La causa è nel programma, non
nell'interprete: la ritrasmissione pubblica prima a EST e poi a SUD, quindi il
vicino di destra riceve mentre quello di sotto sta ancora aspettando la seconda
coppia `OUT`+`SETRDY`, e lo scarto si somma a ogni hop. Scambiando i due
blocchi l'onda si inclina dall'altra parte.

Non è un difetto — il broadcast raggiunge comunque tutti, che è la garanzia
richiesta — ma è la prima volta che si vede che **l'ordine in cui una cella
pubblica le sue direzioni è una scelta di prestazioni**. Nello stencil peserà
sul serio: lì ogni cella aspetta tutti e quattro i vicini prima di calcolare,
quindi il più lento detta il passo dell'intera griglia.

> **Smentita dalla misura (Fase 6, sezione 12.5).** L'ultima frase è sbagliata,
> e per una ragione che vale più della previsione: nello stencil lo spin è
> **esattamente zero**, su qualunque forma di griglia. Non c'è un "più lento"
> perché il kernel è uniforme — tutte le celle eseguono la stessa istruzione
> allo stesso ciclo, quindi il valore che una cella chiede è già stato
> committato quando lo chiede. Il ragionamento qui sopra era corretto ma
> applicato a un programma diverso: valeva per `broadcast.s`, dove le celle
> partono a tempi diversi perché il dato le raggiunge a tempi diversi.

### 9.8 Cosa resta aperto

> Le prime due voci sono state chiuse in Fase 5 (sezione 11) e restano qui
> perché il *perché* erano aperte fa parte del ragionamento; le altre due sono
> ancora aperte.

- ~~**`lw`/`sw` indicizzano per parola, non per byte.**~~ **Chiuso (Fase 5,
  sezione 7.)** La valutazione "rimandabile finché i kernel tengono lo stato
  nei registri" era corretta sul sintomo e sbagliata sulla priorità: le stesse
  righe non controllavano l'ampiezza dell'accesso, quindi uno `sw` fuori RAM
  scriveva nella cella successiva dell'array piatto di `grid_init`. Non era
  solo un indirizzamento da correggere prima dei programmi con `.data`, era una
  falla nell'isolamento fra processori.
- ~~**L'I/O di bordo dell'host non esiste ancora.**~~ **Chiuso (Fase 5,
  sezione 11.)** La diagnosi in scrittura era giusta, quella in lettura no:
  "gli output di bordo sono già raggiungibili, manca solo qualcuno che li
  guardi" sottovaluta il consume-on-read. Raggiungibile non è drenabile —
  guardare `out_ch[d]` senza far avanzare `rp` lascia il canale pieno per
  sempre, e la cella di perimetro si inchioda sulla propria `SETRDY` al secondo
  valore che prova a mandare fuori. Serviva codice nuovo su entrambi i lati.
- **Lo spin costa.** La tabella della sezione 9.6 mostra le attese crescere
  quadraticamente con la lunghezza della catena: ogni cella intermedia consuma
  cicli chiedendo "c'è qualcosa?" a un canale vuoto. È corretto — è il modello
  a spin del ready-bit — ma su una griglia grande andrà quantificato, ed è
  l'argomento a favore di un eventuale meccanismo di attesa passiva se il
  relatore lo chiedesse.
- **Kernel successivi**: ~~`reduce.s`~~ **fatto (sezione 11)** e
  ~~`jacobi.s`~~ **fatto (sezione 12)**. Restava da verificare la tesi del
  progetto — che la sincronizzazione **emerga dal ready-bit** invece che da una
  barriera globale — e lo stencil la dimostra nel modo più forte possibile: non
  solo funziona senza barriera, ma gira con **zero cicli di attesa**. Escluso
  *Game of Life*: richiede i vicini diagonali, che con quattro porte diventano
  instradamento a due hop, triplicando il codice per una proprietà che lo
  stencil dimostra già.

---

## 10. Corrispondenza con la specifica: core o processori?

Domanda legittima dopo quattro programmi: quello che l'array contiene sono
*core* nel senso di un multicore (unità di esecuzione che condividono memoria e
bus) o **processori** indipendenti?

Sono processori. `RISC_V` contiene `uint32_t memory[MEM_SIZE]` — la memoria
è **dentro** la struct, una per cella — oltre ai propri 32 registri e al
proprio `pc`, e `main.c` carica una copia separata dell'ELF nella memoria di
ognuno. Non esiste un solo byte di stato condiviso fra due celle **tranne** il
`Channel`: è una maglia di calcolatori completi che comunicano solo per
messaggi, non un chip multicore.

**Il nome era la parte fuorviante, ed è stato corretto.** La tesi chiamava la
struct `RISCV_Core`, e da lì la parola "core" si era propagata a tutto il
resto: i file `core.c`/`core.h`, le funzioni `init_core` e
`carica_elf_in_core`, il campo `core_id`, l'array `grid->cores`, e la traccia
stessa che stampava `[core 3]`. Un lettore che arriva al codice dopo aver
letto questa sezione trovava scritto ovunque il termine che la sezione appena
gli ha detto di non usare. Ora la sigla è **RISC** dappertutto — `risc.c`/
`risc.h`, `init_risc`, `carica_elf_in_risc`, `risc_id`, `grid->risc`,
`[RISC 3]` — e l'unico posto in cui la parola "core" sopravvive è questa
sezione, dove è l'oggetto del discorso e non un'etichetta. Restano con il nome
storico solo i riferimenti al codice della tesi (`run_multi_core`, `in_reg`/
`out_reg`), che descrivono qualcosa che non esiste più e vanno letti come
citazioni.

Ed è quello che la specifica chiede. Punto per punto:

**"flag di sincronizzazione in termini di coppie di bit, contatori,
confrontatori"** — è esattamente `channel.h`. La coppia di bit sono `wp` e
`rp`, due contatori mod 2; i confrontatori sono le due sole funzioni che
leggono lo stato:
```c
ch_isrdy(c)  ->  c->wp != c->rp     // pieno: il consumatore può leggere
ch_iswrt(c)  ->  c->wp == c->rp     // vuoto: il produttore può scrivere
```
Non è una scelta estetica: è lo schema classico del buffer circolare, dove
`wp`/`rp` sono puntatori di scrittura e lettura e lo stato pieno/vuoto si
deduce dal loro confronto. Con profondità 1 i contatori sono mod 2, cioè
singoli bit — che è la ragione per cui la specifica dice "coppie di bit". Se un
giorno servisse un canale di profondità N, `wp`/`rp` diventano contatori a
`log2(N)+1` bit e cambiano solo i confrontatori: la struttura del protocollo,
e le quattro istruzioni, restano identiche.

Alla lettera della specifica **non è stato aggiunto nessun bit**. Il `pending`
della Fase 4 (sezione 9.3) era l'unica eccezione ed è stato tolto in revisione:
il problema che copriva — `OUT` e `SETRDY` sono due istruzioni separate, quindi
la pubblicazione attraversa un confine di ciclo che sulla carta non esisteva —
è coperto ora dalla cattura del dato sul fronte di `wp`, cioè da un comparatore
sui contatori che ci sono già.

**"implementarli nell'interprete main, che prende una topologia (stringa di 10,
oppure matrice 5x3)"** — sono `grid.c` + `main.c`. La topologia arriva da riga
di comando come `<righe> <colonne>`, quindi la "stringa di 10" è `1 10` e la
"matrice 5x3" è `5 3`: non due casi di codice, un solo cablaggio parametrico
(sezione 8).

**"gli fa fare a tutti un certo numero di cicli fetch-decode-exec, uno alla
volta per tutti"** — è `grid_step`: un `for` sulle celle che esegue **una**
istruzione per ciascuna, poi la fase di commit. Il "uno alla volta"
dell'implementazione è sequenziale, ma non è osservabile dai programmi: grazie
al doppio buffer `_next`, durante l'intera fase compute ogni cella vede lo
stato congelato a inizio ciclo, quindi il risultato non dipende dall'ordine di
scansione (sezione 8). L'array si comporta come se le celle andassero in
parallelo pur essendo simulate in fila.

**"si occupa anche di alimentare i registri di bordo o leggere quelli di bordo
che vengono scritti"** — **coperto in Fase 5** (sezione 11): `grid_push` e
`grid_pop` mettono l'host sui due lati del perimetro, `grid_border_fill` è la
condizione al contorno costante, e `run_grid` le chiama prima di `grid_step`.
Con questo non resta nessun punto della specifica scoperto: le quattro
istruzioni (sezione 5), il protocollo a coppia di bit (sezione 6), la topologia
parametrica e il two-phase (sezione 8), l'I/O di bordo (sezione 11). Quello che
manca è dimostrativo, non architetturale — `jacobi.s`, il kernel 2D che usa
tutte e quattro le porte.

---

## 11. Fase 5 — l'I/O di bordo, e due bug che stavano sotto

La Fase 5 comincia con un controllo di routine su tutto il repository prima di
aggiungere codice nuovo, e questo è il primo fatto da annotare: la build era
pulita, `make test` verde su tutte le combinazioni delle fasi precedenti, e
sotto quel verde c'erano **due segfault-o-peggio** che nessun test toccava. Non
per mancanza di test — le fasi 1-4 sono coperte bene — ma perché entrambi i bug
stavano su percorsi che i test non attraversano: uno nella modalità a singolo
RISC, l'altro in `lw`/`sw`, che nessun kernel usava. Una suite verde dice che
quello che è testato funziona, non che il resto esiste.

### 11.1 I due bug (sezione 7 per il dettaglio)
Sono documentati dove vive il codice, non qui, ma vale riassumere cosa hanno in
comune: **entrambi erano già stati intravisti e archiviati male.**

Il primo — `IN`/`ISRDY` che dereferenziano `in_ch[dir]` a `NULL` — era la
conseguenza non tratta di un ragionamento che il documento faceva già
correttamente per `print_state`. La guardia era stata messa in un posto e non
nell'altro.

Il secondo — `lw`/`sw` indicizzati per parola — era in sezione 9.8 come voce
aperta con la valutazione "rimandabile". Il sintomo descritto era giusto, ma la
valutazione guardava solo lui: nelle stesse righe mancava il controllo di
ampiezza, e quello non era rimandabile perché rompeva l'isolamento fra celle.
La lezione non è "non rimandare", è che **la voce di debito era stata scritta
descrivendo il sintomo invece di leggere le righe**, e una riga che sbaglia un
indirizzo è esattamente la riga da controllare anche per i limiti.

### 11.2 L'host di bordo: dove *non* è servito codice
Il punto di partenza è un'osservazione che ha cancellato la maggior parte del
lavoro previsto. La sezione 9.8 immaginava "una routine dell'host che riempia
`grid->border`", e questo suggerisce un indicizzamento: `grid->border` è un
array piatto di `2*(rows+cols)` canali riempito da `grid_init` in un ordine
suo (per ogni colonna NORD e SUD, poi per ogni riga OVEST ed EST), quindi
sembra servire una mappa da `(r, c, dir)` a `border[idx]` — e una mappa che
duplica una convenzione stabilita altrove è precisamente il tipo di codice che
si disallinea alla prima modifica di `grid_init`.

Non serve, perché la mappa esiste già ed è il cablaggio stesso:

```
il canale di bordo della cella (r,c) verso dir  ==  grid_at(g,r,c)->in_ch[dir]
```

`grid_init` ha già fatto puntare quel campo allo slot giusto in Fase 3. L'host
non deve sapere *dove* sia un canale di bordo, deve solo chiedere alla cella
qual è il suo ingresso in quella direzione — la stessa domanda che si fa il
RISC quando esegue `IN`. Allo stesso modo `grid_step` già committava
`grid->border` insieme agli `out_ch`, quindi nemmeno la fase di commit andava
toccata. Di tutta la clausola restava solo il protocollo.

### 11.3 `grid_push` / `grid_pop` — l'host come vicino
```c
int  grid_push(Grid*, int r, int c, int dir, uint32_t v);   // -> 1 pubblicato, 0 slot pieno
int  grid_pop (Grid*, int r, int c, int dir, uint32_t *v);  // -> 1 c'era un dato, 0 vuoto
void grid_border_fill (Grid*, uint32_t v);                  // alimenta: costante sul perimetro
int  grid_border_drain(Grid*);                              // drena: -> quanti valori usciti
```

`grid_push` fa quello che farebbe un vicino produttore, nell'ordine che fa un
vicino produttore: `ch_write` poi `ch_setrdy`. Il valore di ritorno **è** la
backpressure — se la cella non ha ancora consumato il valore precedente,
`ch_write` viene rifiutata, `ch_setrdy` dà `0`, e il chiamante ritenta il ciclo
dopo. Nessuna semantica nuova da imparare: è lo stesso `0` che una cella vede
dalla propria `SETRDY` quando il vicino è in ritardo.

`grid_pop` è il lato che la sezione 9.8 dava per gratuito, e non lo era. Il
protocollo è consume-on-read: `ch_isrdy` dice se c'è qualcosa, ma è `ch_read_c`
a far avanzare `rp` e liberare lo slot. Un host che "guarda" `out_ch[d]` senza
consumare lascia il canale pieno per sempre — e la cella di perimetro, al
secondo valore che prova a mandare fuori, si blocca sulla propria `SETRDY` in
uno spin infinito. **Il drenaggio non è la stampa dei risultati, è la
condizione perché un kernel che emette fuori dal bordo possa terminare.** È
per questo che in `run_grid` è incondizionato, mentre l'alimentazione è dietro
`BORDO`: uno è una scelta dell'utente, l'altro è una necessità dell'array.

Le due funzioni hanno un `assert` in testa che verifica che `(r, c, dir)` punti
davvero fuori dalla griglia. Il controllo non ricalcola le condizioni sulle
coordinate (`dir == NORD && r == 0`, e le altre tre): confronta il puntatore
col range di `grid->border`, cioè chiede di nuovo al cablaggio invece di
riderivarlo.

```c
static int e_bordo(Grid *g, int r, int c, int dir) {
    Channel *ch = grid_at(g, r, c)->in_ch[dir];
    return ch >= g->border && ch < g->border + 2 * (g->rows + g->cols);
}
```

Serve perché una direzione *interna* passata per errore non darebbe un errore:
funzionerebbe, iniettando dati in un canale fra due celle come se li avesse
mandati il vicino. Un array systolic in cui i dati appaiono da un vicino che
non li ha mai spediti è il tipo di bug che si insegue per giorni guardando il
programma sbagliato.

`grid_border_fill` e `grid_border_drain` sono scritte **sopra `grid_push`/
`grid_pop`**, non sopra `grid->border`. Iterare l'array diretto sarebbe stato
più corto di quattro righe, ma avrebbe messo il protocollo in due posti; così il
perimetro lo enumerano solo loro e il protocollo resta in uno.

`grid_border_drain` scarta i valori e ne restituisce solo il conteggio, e in
Fase 6 quella scelta si è rivelata quella giusta per una ragione non prevista:
lo stencil spinge fuori da tutti e quattro i lati a ogni iterazione, quindi
stamparli uno per uno — che è quello che `run_grid` faceva in Fase 5 — sommerge
la traccia con migliaia di righe (1024 su una 4x4 con `ITER=64`). Chi vuole un
valore preciso ha `grid_pop` sulla direzione che gli interessa, come fa
`tests/test_reduce.c` sull'angolo sud-est. Il drenaggio è una condizione di
terminazione, non una funzione di visualizzazione — e conviene che il tipo di
ritorno lo dica.

### 11.4 Quando l'host parla: prima di `grid_step`
```c
if (bordo) grid_border_fill(grid, strtoul(bordo, NULL, 0));   // BORDO=<n>: opzionale
usciti += grid_border_drain(grid);                            // sempre
grid_step(grid);
```
L'ordine non è arbitrario, ed è l'unica scelta che preserva la proprietà per
cui esiste il two-phase. Chiamandole **prima** di `grid_step`, le scritture
dell'host finiscono nel `_next` esattamente come quelle dei RISC e diventano
visibili al commit di quel ciclo: un valore spinto al ciclo *k* è leggibile
dalla cella di perimetro al ciclo *k+1*, mai prima. L'host paga un ciclo di
latenza per hop come qualunque cella, cioè **non è un canale privilegiato**: è
un nodo in più ai margini della maglia. Se lo si chiamasse dopo `grid_step`
funzionerebbe ancora — il commit successivo raccoglierebbe le scritture — ma
l'host starebbe agendo in una finestra temporale che nessuna cella ha, e la
fedeltà temporale che la sezione 8 difende sarebbe vera per tutti tranne uno.

`BORDO=<n>` segue la convenzione di `STEP`, già presente in `run_grid`, e
riempie ogni ingresso di perimetro con la stessa costante a ogni ciclo: è la
condizione di Dirichlet dello stencil (temperatura fissa ai margini), che era
lo scopo dichiarato. Senza `BORDO` i canali di bordo restano vuoti per sempre —
un bordo aperto da cui non arriva niente, che è il comportamento su cui i
kernel delle fasi 1-4 si appoggiavano. Un contorno che varia per cella o nel
tempo non ha bisogno di un'estensione della sintassi: si scrive con `grid_push`
da C, come fa `tests/test_bordo.c`.

### 11.5 `bordo.s` — il programma che i test di prima non potevano essere
Sette istruzioni: aspetta un valore da NORD, ripetilo a SUD, fermati. Su una
griglia RxC ogni colonna diventa una catena verticale indipendente.

La ragione per cui è questo il programma che verifica l'I/O di bordo è che
**entrambi i capi della catena cadono fuori dalla griglia**: la prima riga
aspetta su NORD un vicino che non esiste, l'ultima pubblica su SUD verso
nessuno. È l'esatto opposto della strategia dei tre kernel precedenti, che
generavano il dato *dentro* la griglia proprio per non dipendere dall'host —
l'aggiramento che la sezione 9.8 chiamava "un aggiramento, non una soluzione".
Nessuno di quei tre programmi può fallire se `grid_push` è rotta, perché
nessuno legge da fuori.

E `bordo.s` non è simmetrico solo per eleganza: nota che l'ultima riga **non ha
un caso speciale**. Pubblica su SUD come tutte le altre, e chi la drena è
l'host al posto della cella che non c'è. Se `grid_pop` non consumasse davvero,
l'ultima riga si bloccherebbe e il test morirebbe sul tetto dei cicli invece di
dare un valore sbagliato — il modo migliore in cui un test può fallire.

`tests/test_bordo.c` asserisce tre cose: che il valore arrivi in fondo a ogni
colonna (la spinta funziona), che ne esca **esattamente uno per colonna** (il
drenaggio funziona e non duplica), e che una seconda `grid_push` nello stesso
ciclo torni `0` (l'invariante della sezione 6). Verde su 1x1, 1x6, 6x1, 3x4,
4x4 — le stesse forme di `broadcast.s`, perché isolano i casi degeneri.

### 11.6 `reduce.s` — il primo kernel che combina
Primo programma in cui i dati non vengono instradati ma **sommati**, in due
fasi che sono lo schema classico della riduzione su array:

```
1. ogni RIGA si somma da OVEST a EST        -> l'ultima colonna ha i totali di riga
2. l'ULTIMA COLONNA si somma da NORD a SUD  -> (R-1,C-1) ha il totale generale
```

Il percorso è lungo `(C-1)+(R-1)` hop invece del logaritmo di un albero di
riduzione vero. Non è un'inefficienza da correggere: con quattro porte e solo
vicini adiacenti, l'albero **non è cablabile** — le sue foglie distanti
dovrebbero parlare con nodi che non toccano. La topologia impone la forma
dell'algoritmo, che è esattamente il tipo di vincolo che questo progetto esiste
per mostrare.

Due proprietà volute nel modo in cui è scritto. Primo, il contributo della
cella è `r+c` e non una costante: così il totale atteso **si ricava dalla forma
della griglia** invece di essere una costante nel test (stessa scelta della
somma di `prodcons`, sezione 9.4), e una cella cablata al posto sbagliato
cambia il risultato invece di passare inosservata. Secondo, nessun caso
speciale in nessuno dei quattro estremi: agli ingressi la colonna 0 e la riga 0
sanno di non avere un predecessore dalla propria identità cablata (`a0`/`a1`,
sezione 7), e in uscita l'ultima riga pubblica su SUD come tutte le altre
perché a leggere il risultato è l'host — di nuovo `grid_pop`. Il risultato del
calcolo esce dalla griglia, non resta in un registro da ispezionare: è la prima
volta che l'array produce un output nel senso della specifica.

`tests/test_reduce.c` asserisce dal locale al globale, così un fallimento dice
*dove* si è rotta la riduzione: ogni cella non-ultima-colonna ha il prefisso
della sua riga, ogni cella dell'ultima colonna ha le righe `0..r` sommate per
intero, e dal bordo sud-est esce un solo valore ed è il totale. Verde sulle
stesse cinque forme: `1x1 -> 0`, `1x6 -> 15`, `6x1 -> 15`, `3x4 -> 30`,
`4x4 -> 48`.

### 11.7 `memtest.s` — il test che non ha richiesto un harness
La verifica di `lw`/`sw` non ha un harness C proprio, e non per pigrizia:
`test_broadcast` **è già** un "esegui questo programma su una griglia RxC e
asserisci `s1` in ogni cella", il nome dice broadcast solo perché è nato lì.
`memtest.s` si autoverifica — ogni controllo che fallisce salta a un'etichetta
che azzera `s1` — quindi un'unica asserzione su `s1` copre tutti i passi, ed è
esattamente la forma che quell'harness sa già eseguire.

Copre i tre difetti separatamente: che `lw` rilegga ciò che `sw` ha scritto,
che gli offset siano byte (i quattro byte di un word stanno a `0..3`,
little-endian), che i word adiacenti distino 4 e non 1, che `sb` cambi un solo
byte dentro un word, e che `lh`/`lhu` differiscano sull'estensione del segno.

L'ultima parte merita una nota, perché verifica una guardia il cui effetto è
*fermare* il RISC: `memtest.s` scrive `s1` col valore buono, **poi** fa un
accesso fuori RAM, e dopo di quello mette due istruzioni che azzererebbero
`s1`. Se la guardia funziona il RISC si ferma sull'accesso e `s1` sopravvive;
se manca, l'esecuzione prosegue e il test fallisce. Verificato in negativo:
rimettendo l'indicizzazione a parola, `make test-mem` fallisce sull'assert.
Un test che non è stato visto fallire non è ancora un test.

### 11.8 Stato
`make test` verde su tutto: canale, memoria, `prodcons` × 6 ritardi, `chain` ×
6 ritardi × 4 lunghezze, `broadcast` × 5 forme, `bordo` × 5 forme, `reduce` × 5
forme. Build senza warning con `-Wall -Wextra`.

Resta `jacobi.s` — sezione 12.

---

## 12. Fase 6 — `jacobi.s`: lo stencil, e il conto che torna a zero

Jacobi a 5 punti: `u(r,c) ← media dei quattro vicini`, iterato. Una cella per
punto della griglia, il valore in `s1`. È il kernel 2D principale previsto dalla
specifica (§6) e chiude la Fase 4 delle dimostrazioni.

### 12.1 Che sistema lineare risolve, e dove sta la matrice

Domanda naturale, perché sembra un paradosso: **Jacobi è definito su matrici
quadrate, come può girare su una griglia 3×4?** Il paradosso sparisce notando
che sono in gioco *due* matrici diverse, e la griglia non è nessuna delle due.

La griglia è il **dominio**: un pezzo di piano rettangolare discretizzato.
Laplace su un rettangolo è ben posto, non c'è niente che chieda un quadrato. La
matrice del sistema, quella su cui Jacobi opera, ha una riga e una colonna per
**incognita**, cioè per cella — quindi è `N × N` con `N = R·C`, quadrata per
costruzione. Una griglia 3×4 sono 12 incognite e una matrice **12×12**; la forma
`3×4` è la forma del vettore delle incognite *ripiegato in griglia*, non la forma
della matrice.

Scritta per esteso per `R=3, C=4, BORDO=64`:

```
A (Laplaciano a 5 punti, 12x12)          b
  4 -1  0  0 -1  0  0  0  0  0  0  0    128     angoli: due vicini di bordo
 -1  4 -1  0  0 -1  0  0  0  0  0  0     64     lati:   uno
  0 -1  4 -1  0  0 -1  0  0  0  0  0     64
  0  0 -1  4  0  0  0 -1  0  0  0  0    128
 -1  0  0  0  4 -1  0  0 -1  0  0  0     64
  0 -1  0  0 -1  4 -1  0  0 -1  0  0      0     interno: nessuno
  0  0 -1  0  0 -1  4 -1  0  0 -1  0      0
  0  0  0 -1  0  0 -1  4  0  0  0 -1     64
  0  0  0  0 -1  0  0  0  4 -1  0  0    128
  0  0  0  0  0 -1  0  0 -1  4 -1  0     64
  0  0  0  0  0  0 -1  0  0 -1  4 -1     64
  0  0  0  0  0  0  0 -1  0  0 -1  4    128
```

Quel `b` **è** la condizione al contorno, e chi lo fornisce è
`grid_border_fill`: 128 agli angoli (due vicini di bordo × 64), 64 sui lati
(uno), 0 al centro (nessuno).

Jacobi standard con `D = diag(A) = 4I` è
`u⁽ᵏ⁺¹⁾ = D⁻¹(b − (A−D)u⁽ᵏ⁾)`. Espandendo la riga *i*:
`u_i ← (1/4)(b_i + Σ u_vicini)` — che riga per riga è `addi s2,s2,2` +
`srai s1,s2,2` dopo aver sommato i quattro `IN`. **Stessa iterazione, scritta
due volte.** Verifica incrociata: l'iterazione matriciale sulla 12×12, con lo
stesso arrotondamento intero, converge a *k*=16, esattamente il
`3x4 seme=0 -> campo finale a k=16` che misura l'array.

#### La matrice non esiste da nessuna parte
È il punto che vale più della derivazione. L'array non memorizza `A`: la matrice
**è il cablaggio**.

| oggetto in `A` | dov'è nell'array |
|---|---|
| una riga di `A` | una cella |
| il 4 sulla diagonale | lo shift di 2 bit |
| ogni −1 fuori diagonale | **un canale** |
| il pattern di sparsità | la topologia di `grid_init` |
| il vettore `b` | i valori che l'host spinge sul bordo |

Nessuna cella conosce `A`, e infatti `jacobi.s` non legge nemmeno `a2`/`a3`: non
sa quanto è grande il sistema che sta risolvendo. È la stessa cosa detta in 12.2
(la geometria nel cablaggio, non nel programma), vista dal lato dell'algebra
lineare — **il pattern di sparsità di una matrice 12×12 è il cablaggio di 12
processori.**

#### Perché converge su qualunque R, C
La condizione per Jacobi è la dominanza diagonale, e sulla 3×4 la misura dà
`|a_ii| > Σ|a_ij|` in **10 righe su 12**, `≥` in tutte e 12. Le 2 celle interne
hanno esattamente 4 vicini interni, quindi `4 = 4`: dominanza solo *debole*. Le
10 celle adiacenti al bordo ne hanno meno di 4 → dominanza *stretta*. Con il
grafo della griglia connesso questo è **irriducibilmente diagonalmente
dominante**, che basta per la convergenza. In forma di Kronecker si vede a
occhio che vale per ogni coppia di dimensioni:
`A = I_R ⊗ T_C + T_R ⊗ I_C` con `T_k = tridiag(−1,2,−1)`.

E le forme degeneri confermano la teoria invece di essere casi speciali —
il numero di iterazioni segue la struttura di `A`, che segue la forma della
griglia:

| forma | righe a dominanza stretta | *k* misurato |
|---|---|---|
| 1×1 | 1/1 (`A = [4]`, `b = 4B`) | 1 |
| 1×6 | 6/6 (max 2 vicini interni) | 6 |
| 4×4 | 12/16 (4 celle interne) | 18 |

### 12.2 Perché è il primo kernel *uniforme*
Nel testo di `jacobi.s` non c'è nessun salto condizionato la cui condizione
dipenda da `a0`/`a1`/`a2`/`a3`. Tutti i kernel precedenti fanno il contrario:

| kernel | come sceglie il ruolo |
|---|---|
| `prodcons.s` | `mv s0, a1` · `beqz s0, producer` — due ruoli |
| `chain.s` | `beqz s0, source` · `beq s0, s5, sink` — tre ruoli |
| `broadcast.s` | `add t3, a0, a1` · `beqz t3, source` — sorgente vs ripetitore |
| `reduce.s` | tre rami su `a1`, `a3-1`, `a0` |
| `jacobi.s` | **nessuno**: `a0..a3` non compaiono nel controllo di flusso |

I rami, nei kernel precedenti, erano lì per **due ragioni indipendenti**, e
distinguerle spiega perché solo ora si ottiene l'uniformità:

- **L'algoritmo ha ruoli distinti.** Una riduzione ha una radice, un broadcast
  una sorgente, una catena un produttore e un pozzo. Sono asimmetrie *della
  matematica*: nessun supporto dell'host le elimina. `reduce.s` avrà sempre
  bisogno di sapere se è l'ultima colonna.
- **Il bordo costringeva a farlo.** Prima della Fase 5 "leggi da nord" era
  un'operazione **non definita** per la riga 0: `ISRDY` tornava `0` per sempre,
  e l'unica difesa era che la cella riconoscesse di stare sul perimetro. È
  l'aggiramento che la sezione 9.8 chiamava per nome.

Jacobi è il primo kernel in cui la prima ragione **non si applica** — ogni punto
fa la stessa media, nessuna cella è privilegiata — quindi togliere la seconda
compra l'uniformità completa. Su `reduce.s` la Fase 5 avrebbe tolto un ramo su
tre; qui li toglie tutti.

Attenzione a cosa significa: è uniforme il **testo del programma**, non
l'esecuzione. Le celle di perimetro leggono una costante da fuori, quelle
interne valori calcolati da vicini reali. La differenza si è spostata dal codice
alla topologia — che è la tesi del progetto in miniatura: la geometria nel
cablaggio, la matematica nel programma. Si vede meglio nell'inversione col
riferimento del test, che ha i quattro rami sulla posizione
(`r == 0 ? bordo : ...`) che l'assembly non ha: **il modello di riferimento è
più complicato del programma che verifica.**

Nota anche che il campo iniziale `SEME=1` fa `add s1, a0, a1` e **non** rompe la
proprietà: è aritmetica sulla posizione, non controllo di flusso. Resta un solo
percorso di esecuzione per tutte le celle.

### 12.3 L'ordine dei quattro OUT è una condizione di correttezza
Con canali di profondità 1, l'ordine dei due blocchi decide se il kernel
funziona o si inchioda. Le tre possibilità:

- **Ricevi-poi-spedisci** → deadlock al primo giro, tutti aspettano un valore
  che nessuno ha spedito.
- **Alternato per direzione** (`OUT N, IN N, OUT E, IN E, …`) → si sblocca, ma
  solo per come sono disposti i bordi, e l'argomento di correttezza dipende
  dall'ordine. Fragile.
- **Spedisci tutti e quattro, poi ricevi tutti e quattro** → dimostrabilmente
  senza deadlock.

La dimostrazione, che vale la pena avere scritta prima di scoprirla con
`MAX_CICLI`:

> Supponi tutte le celle bloccate per sempre. Una cella bloccata in
> **spedizione** all'iterazione *k* aspetta che il vicino consumi il valore di
> *k−1*, quindi aspetta una cella a iterazione **≤ k−1**; una bloccata in
> **ricezione** a *k* aspetta una cella a iterazione **≤ k**. Prendi `k_min`,
> l'iterazione minima nella griglia. Nessuna cella a `k_min` può essere
> bloccata in spedizione, perché aspetterebbe qualcuno sotto il minimo. Quindi
> tutte le celle a `k_min` sono in ricezione. Ma **una cella in fase di
> ricezione ha già completato la propria fase di spedizione**: ha già pubblicato
> tutti e quattro i suoi valori. Allora ciò che aspetta c'è già, e non è
> bloccata. Contraddizione. ∎

Il passo che regge tutto è l'ultimo, ed è esattamente la ragione per cui i
quattro `OUT` vanno prima dei quattro `IN` e non intercalati.

Una precisazione che è facile sbagliare: **è Jacobi e non Gauss-Seidel per la
struttura del programma, non per il doppio buffer di `channel.h`.** Il `_next`
serve al determinismo a livello di ciclo (l'ordine di scansione di `grid_step`
non è osservabile, sezione 8). Jacobi viene dal fatto che la cella *spedisce il
valore vecchio prima di calcolare quello nuovo*: invertendo i passi 1 e 3 si
otterrebbe Gauss-Seidel con un ordine dipendente dalla topologia, con lo stesso
`channel.h` sotto.

### 12.4 L'arrotondamento: una istruzione fra risolvere Jacobi e non risolverlo
`rv32i` non ha `div` (il RISC la implementa, l'assembler con `-march=rv32i` la
rifiuta), quindi la divisione per 4 è uno shift. Ma `srai` secco **tronca**, e
la troncatura è un bias sistematico verso il basso che crea un **punto fisso
spurio**: quando i quattro vicini valgono `B−1` il risultato resta `B−1`, quindi
il campo si ferma sotto la soluzione e non ci arriva più. Misurato su 4x4 con
`BORDO=64`, la cui soluzione esatta è 64 su tutta la griglia:

| variante | campo a `ITER=64` (righe 0 e 1) |
|---|---|
| `srai s1, s2, 2` | `[62,61,61,62] [61,60,60,61]` — **stallo, mai 64** |
| `srai`, punto fisso a 8 bit frazionari | `[63,63,63,63]` — meglio, ancora non esatto |
| `addi s2,s2,2` + `srai` | `[64,64,64,64] [64,64,64,64]` — **esatto** |

Non è imprecisione, è un kernel che converge alla risposta sbagliata. Una
istruzione (`addi s2, s2, 2`, arrotondamento al più vicino) la elimina, e la
somma di quattro valori sta larga fino a `BORDO ≈ 2^29` senza overflow.

Sul **segno**: con l'arrotondamento e `BORDO ≥ 0` tutto resta non negativo per
sempre, quindi la differenza fra `srai` (arrotonda verso −∞) e `div` (verso
zero) non si presenta mai. Con bordi negativi `srai` resta quello giusto, e il
suo arrotondamento è una proprietà da documentare, non un bug.

Il punto delicato è che **nessuno di questi difetti fa fallire l'asserzione
principale**, perché il riferimento in C rispecchia l'aritmetica bit per bit: se
sbagli in entrambi i posti, il confronto torna. Per questo il test ha una
seconda asserzione — se il riferimento raggiunge un punto fisso, quel punto
fisso deve valere `bordo` su tutta la griglia. È lei che guarda la decisione, e
verificata in negativo: togliendo il `+2` da kernel *e* riferimento insieme,
l'asserzione bit-per-bit passa e quella sul punto fisso cade.

### 12.5 Il risultato che non era previsto: spin zero
`make test-jacobi` su cinque forme e due campi iniziali dà lo stesso numero di
cicli per tutte:

```
1x1  1x6  6x1  3x4  4x4   ->  2115 cicli, sempre
```

Il corpo del loop è 33 istruzioni (4 `OUT`+`SETRDY`+branch = 12, un `li`, 4
`ISRDY`+branch+`IN`+`add` = 16, più 4 di media e controllo). Con `ITER=64`:
`64 × 33 + 2 di setup + 1 ecall = 2115`. **Spin: esattamente zero cicli**, su
ogni forma.

Non è fortuna, è strutturale, ed è la conseguenza diretta dell'uniformità.
Siccome tutte le celle eseguono la stessa istruzione allo stesso ciclo, quando
una cella arriva alla sua fase di ricezione (ciclo relativo 13) il vicino ha
pubblicato al ciclo 3 e il valore è committato dal 4: `ISRDY` trova sempre
pronto. E all'inizio di ogni iterazione tutti i canali di uscita sono vuoti,
perché i vicini hanno consumato tutto nella fase di ricezione dell'iterazione
precedente: `SETRDY` riesce sempre al primo colpo.

Questo **smentisce la previsione della sezione 9.7** ("lì il più lento detta il
passo dell'intera griglia"). Il ragionamento era giusto ma applicato al
programma sbagliato: vale per `broadcast.s`, dove le celle partono a tempi
diversi perché il dato le raggiunge a tempi diversi. In un kernel uniforme
**non esiste un più lento**. È anche il risultato più forte possibile per la
tesi del progetto: la sincronizzazione non solo emerge dal ready-bit senza
barriera globale, ma non costa niente.

Confronto con la tabella della sezione 9.6, dove le attese di `chain.s`
crescevano quadraticamente con la lunghezza della catena: lo spin è il prezzo
dell'**asimmetria**, non del protocollo.

### 12.6 La convergenza vive nell'host, non nell'array
Il criterio naturale (`while err > tol`, l'idioma di MATLAB) in un array è
**molto** più costoso di quanto sembri, perché la convergenza è una proprietà
globale e una cella non la può conoscere. Servirebbe, a ogni iterazione: una
riduzione di `max|u_new − u|` verso un angolo (`reduce.s` innestato), *più* un
broadcast del verdetto indietro a tutte le celle perché devono fermarsi insieme
(`broadcast.s` innestato). Costo di coordinamento `≈ 2((R−1)+(C−1))` hop contro
33 cicli di calcolo effettivo: su 4x4 il controllo domina il calcolo. E peggio,
la riduzione ha bisogno di una radice e il broadcast di una sorgente, quindi
rientrerebbero i rami sulla posizione — si perderebbe la proprietà di 12.2 per
un criterio di arresto.

La soluzione è più semplice di entrambe le alternative che avevo considerato
(spiare `s3` ciclo per ciclo, o rieseguire la griglia per ogni *k*): **siccome
il riferimento in C è esatto bit per bit, `max|u_k − u_k−1|` calcolato lì *è* la
convergenza dell'array.** Non serve osservare niente a runtime. Il test stampa
la tabella, tu leggi a quale iterazione scende sotto la tua tolleranza, e quello
è l'`ITER` con cui compilare. È il ciclo `while` di MATLAB srotolato, e a
renderlo affidabile è l'asserzione che lega il riferimento all'array.

**Quale *k* riportare, però, è una scelta che si può sbagliare, e la prima
versione la sbagliava.** `delta` all'iterazione *k* è `max|u_k − u_k−1|`, quindi
va a zero la prima volta che un'iterazione **non cambia niente** — cioè un giro
*dopo* che il campo ha raggiunto il suo valore definitivo. Riportare quel *k*
significa riportare la prima iterazione inutile, e chi compila con quel numero
ne fa una di troppo. Il numero utile è **l'ultima iterazione che ha cambiato
qualcosa**, verificato contro la misura diretta (il primo `ITER` con cui la
griglia esce con tutte le celle a `bordo`):

| forma | primo `ITER` col campo == 64 | primo *k* con `delta` == 0 |
|---|---|---|
| `1x1` | 1 | 2 |
| `1x6`, `6x1` | 6 | 7 |
| `3x4` (`SEME=1`/`0`) | 15 / 16 | 16 / 17 |
| `4x4` | 18 | 19 |

Cresce col diametro della griglia, come deve — ed è il punto di 12.2
sull'informazione che avanza di una cella per iterazione.

Il test stampa anche la **traccia dei delta** fino al primo zero, che è ciò che
serve davvero a una tolleranza qualunque invece di un singolo numero. Per la
`4x4` con `SEME=0`:

```
delta: 32 12 10 9 7 6 5 4 3 3 2 2 1 1 1 1 1 1 0
```

Si legge il *k* per qualsiasi `tol`: `delta ≤ 2` a *k*=11, `≤ 1` a *k*=13, `= 0`
a *k*=18. È il `while err > tol` di MATLAB srotolato in una tabella, che è
esattamente quello che serve quando il criterio di arresto non può vivere
nell'array (sopra).

Una nota sull'alternativa scartata di far *fermare* la griglia all'host: non
solo è inutile, è rischiosa. Fermarla a metà iterazione lascia le celle in stati
incoerenti (alcune hanno spedito e non ricevuto), e riconoscere dall'esterno un
confine di iterazione richiede di leggere `s3` in tutte le celle sperando di
beccare l'istante in cui sono allineate. Osservare senza fermare elimina il
problema in partenza.

### 12.7 Il campo iniziale, e un vincolo che vale sapere
Due modalità, entrambe tenute (`SEME=0|1` via `--defsym`): interno freddo a 0, o
campo iniziale `r+c`. Il vincolo emerso valutandole: **l'host non può seminare
una cella interna.** Ha canali solo verso il perimetro, quindi "l'host fornisce
il campo iniziale" non è sul tavolo con quattro porte, se non per le celle di
bordo. Le due opzioni scartate:

- **Scrivere `regs[9]` da C** nell'harness: funziona, ma inietta stato dentro un
  processore senza passare da un messaggio, cioè contraddice la proprietà su cui
  poggia tutta la sezione 10. Ammissibile in un test, da non usare in una demo.
- **Un array in `.data`**: ora che `lw`/`sw` funzionano è tecnicamente
  possibile, ma ogni cella riceve *la stessa copia* dell'ELF, quindi leggere il
  proprio elemento richiede l'indice `r*cols+c`, cioè `mul`, che
  `-march=rv32i` rifiuta. Si farebbe per somme ripetute. Da valutare col
  relatore se servisse davvero, come banco di prova sul caricamento ELF.

### 12.8 Stato
`make test` verde su tutto, `-Wall -Wextra` senza warning: canale, memoria,
`prodcons` × 6 ritardi, `chain` × 6 ritardi × 4 lunghezze, `broadcast` × 5
forme, `bordo` × 5 forme, `reduce` × 5 forme, `jacobi` × 5 forme × 2 campi
iniziali.

Tutti i punti della specifica sono coperti, dimostrativi compresi. Quello che
resta è misura e scrittura, non architettura: la tabella cicli/forma dello
stencil accanto a quella di `chain.s`, e la decisione col relatore su `.data`
per cella (12.7) se si vuole un banco di prova sul caricamento ELF.
