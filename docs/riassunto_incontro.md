# NESO — riassunto generale del lavoro svolto

Documento di supporto all'incontro. Ogni sezione dice **come funziona**, **perché
è stato fatto così** e **quale alternativa è stata scartata**: le domande più
probabili sono raccolte nell'ultima sezione, con la risposta già pronta.

Tutti i numeri citati vengono da `docs/dati/*.csv`, rigenerati con `make dati`.
Nessuno è scritto a mano.

---

## 1. Cos'è NESO in un paragrafo

NESO è un simulatore di un **array bidimensionale di processori RISC-V (RV32I)**.
Ogni cella è un core completo con i suoi 32 registri, il suo PC e la sua RAM
privata da 16 KB. Le celle non condividono memoria: comunicano **solo con i
quattro vicini ortogonali** (nord, est, sud, ovest) attraverso canali
monodirezionali di **profondità 1** dotati di *handshake*. Il tempo è globale e
discreto: a ogni ciclo di clock ogni core vivo esegue **esattamente una
istruzione**.

Il punto di partenza è l'interprete a singolo RISC della tesi Stella. Da lì:

| Cosa | Stato |
|---|---|
| `src/risc.c`, `src/risc.h` | ex `core.c`/`core.h`, esteso con le quattro istruzioni di comunicazione |
| `src/channel.h` | **nuovo** — il canale e il suo protocollo |
| `src/grid.c`, `src/grid.h` | **nuovo** — topologia, clock globale, I/O di bordo |
| `src/elf.c`, `src/main.c` | caricatore ELF e driver, adattati alla griglia |
| `asm/*.s` | 8 kernel |
| `tests/*.c` | 7 harness di verifica end-to-end |

La tesi di fondo che il progetto vuole dimostrare è una sola:

> **Un bit di *ready* per canale sostituisce la schedulazione temporale
> esplicita.** In un array sistolico classico chi alimenta l'array deve sapere
> *a quale ciclo* consegnare ogni dato; con il ready bit non deve saperlo, e il
> risultato non dipende più dalla velocità relativa dei nodi.

Il paragone quantitativo con il caso senza ready bit esiste ed è misurato
(modalità `NOBP=1`, sezione 5).

---

## 2. Il canale — il cuore del progetto

File: [src/channel.h](src/channel.h) (110 righe, tutto `static inline`).

### 2.1 La struttura

```c
typedef struct Channel {
    uint32_t data;              /* visibile al consumatore: catturato alla transizione di wp */
    uint8_t  wp, rp;            /* write pointer e read pointer, mod 2 */

    uint32_t data_next;         /* registro di uscita: lo carica OUT */
    uint8_t  wp_next, rp_next;
} Channel;
```

**Tutto lo stato di protocollo sono due contatori a un bit e i comparatori che
li confrontano.** Nessun altro flag: è la forma richiesta esplicitamente in sede
di revisione, ed è quella che si cabla direttamente in hardware.

Il canale è **fisicamente condiviso**: `OUT[EST]` del mittente *è* lo stesso
oggetto di `IN[OVEST]` del ricevente. Il cablaggio in `grid_init` non copia
niente, assegna puntatori ([grid.c:37-55](src/grid.c#L37-L55)).

### 2.2 Stato codificato da due contatori mod 2

```c
pieno / leggibile  :  wp != rp
vuoto / scrivibile :  wp == rp
```

**Perché non un semplice `bool pieno`?** Perché un flag booleano dovrebbe essere
scritto da **due lati diversi**: il produttore lo alza quando pubblica, il
consumatore lo abbassa quando legge. Se entrambe le cose accadono nello stesso
ciclo (caso normalissimo: il vicino consuma mentre io pubblico), le due scritture
si contendono lo stesso campo e il risultato dipende dall'ordine in cui il
simulatore visita le celle — cioè da un dettaglio implementativo che nel hardware
reale non esiste.

Con due contatori, **ogni lato scrive solo il proprio campo**: il produttore
tocca solo `wp`, il consumatore solo `rp`. Nessun conflitto, nessuna dipendenza
dall'ordine di scansione. Il confronto `wp != rp` ricostruisce lo stato senza
che nessuno debba scrivere una variabile comune.

Sono `mod 2` (cioè `x ^= 1`) perché la profondità è 1: servono solo due stati
distinguibili, e un contatore a un bit basta e non può mai traboccare.

### 2.3 Il doppio buffer e il commit a due fasi

Ogni campo ha il suo gemello `_next`. **Le letture guardano sempre lo stato
attuale, le scritture vanno sempre nel `next`.** Il ciclo di clock è:

```c
void grid_step(Grid *grid) {
    for (ogni cella)  execute_step(...);   /* FASE COMPUTE: legge lo stato attuale,
                                              scrive solo nei campi _next */
    for (ogni canale) ch_commit(...);      /* FASE COMMIT: _next diventa attuale */
}
```
([grid.c:76-94](src/grid.c#L76-L94))

**Perché non aggiornare direttamente?** Perché senza la separazione il
comportamento dipenderebbe dall'ordine di visita dell'array flat: una cella già
eseguita vedrebbe il valore *nuovo* del vicino, una non ancora eseguita quello
*vecchio*. Lo stesso programma darebbe risultati diversi se si cambiasse
`for (r) for (c)` in `for (c) for (r)`.

Con il commit separato la semantica è quella di un circuito sincrono reale:
tutti leggono lo stato al fronte *k*, tutti scrivono lo stato al fronte *k+1*.
**Conseguenza misurabile: ogni hop costa esattamente un ciclo, sempre.**

I due contatori passano sempre; **il dato solo sulla transizione di `wp`**:

```c
static inline void ch_commit(Channel *c) {
    if (c->wp_next != c->wp) {      /* il confronto È il segnale di pubblicazione */
        c->data = c->data_next;
    }
    c->wp = c->wp_next;
    c->rp = c->rp_next;
}
```

Non sono due buffer generici, sono **due registri in serie**: `data_next` è il
registro di uscita del mittente, `data` è quello che il vicino legge, e l'enable
del secondo è il fronte di `wp`. Senza quella condizione una `OUT` a canale pieno
cancellerebbe un valore pubblicato e non ancora letto.

### 2.4 OUT e SETRDY: due istruzioni, un solo messaggio

Questo è il punto su cui c'è stata la sua indicazione vincolante, ed è
implementato esattamente così:

```c
/* OUT: carica il registro di uscita. Non fallisce mai. */
static inline void ch_write(Channel *c, uint32_t v) {
    if (c -> wp_next == c -> wp) {      /* congelato se la pubblicazione è già decisa */
        c -> data_next = v;
    }
}

/* SETRDY: l'unica che può essere rifiutata, e a rifiutarla è il comparatore. */
static inline int ch_setrdy(Channel *c) {
    if (ch_iswrt(c) && c -> wp_next == c -> wp) {
        c -> wp_next = c -> wp ^ 1;
        return 1;
    }
    return 0;
}
```

**`OUT` non è un invio.** È il caricamento di un registro di uscita. Il canale
garantisce qualcosa solo **da `SETRDY` in poi**: *ogni valore pubblicato viene
consegnato esattamente una volta*. Due `OUT` consecutive senza `SETRDY` in mezzo
sono **due caricamenti dello stesso registro, non due messaggi**: il primo valore
viene semplicemente sovrascritto e nessuno se ne accorge, perché non era ancora
stato promesso a nessuno.

Questa asimmetria è deliberata e va difesa così: **il produttore deve poter
preparare un valore senza impegnarsi a consegnarlo**. È la stessa distinzione fra
un registro di uscita e il segnale di *valid* in un handshake hardware.

E il contratto è simmetrico anche dall'altra parte: **una `SETRDY` senza `OUT`
davanti ripubblica quello che nel registro c'era già**. Caricarlo è compito della
`OUT`, non del canale; accorgersene richiederebbe ricordare che una `OUT` è
passata di lì, cioè esattamente lo stato in più che non si vuole.

### 2.5 Il problema che il progetto deve risolvere, e come lo risolve

`OUT` e `SETRDY` sono **due istruzioni in due cicli distinti** (un core esegue
una istruzione per ciclo), e nel mezzo il consumatore può leggere. Da lì nasce
l'unico caso davvero scomodo del protocollo:

```
t    OUT s1, EST     -> il canale è pieno
     il consumatore legge -> rp commuta al commit -> al ciclo dopo è vuoto
t+1  SETRDY t0, EST  -> ora il canale è vuoto, quindi riesce. Ma pubblica CHE COSA?
```

Se il registro di uscita non fosse stato caricato al ciclo *t*, la `SETRDY`
consegnerebbe il valore **vecchio**, già consegnato una volta: un **duplicato**,
e il valore nuovo non partirebbe mai. Sarebbe un errore silenzioso.

**La soluzione è togliere il gate dalla `OUT`, non aggiungere memoria.** La `OUT`
carica sempre `data_next`, anche a canale pieno, ed è innocuo perché il
consumatore guarda `data`, non `data_next`. Quindi al ciclo *t* il valore nuovo è
già nel registro, e la `SETRDY` di *t+1* consegna esattamente lui.

**Invariante:** `data` cambia **solo** quando `wp` commuta.

*Dimostrazione:* `data` è scritto in un solo punto, la condizione
`wp_next != wp` di `ch_commit`. Un valore pubblicato resta quindi intatto finché
non viene pubblicato il successivo, e pubblicare richiede `wp == rp`, cioè che il
consumatore abbia letto. Nessuna `OUT`, per quante ne faccia il produttore, può
raggiungere `data` senza passare da una `SETRDY` riuscita. ∎

**Corollario operativo:** una `OUT` non può mai fallire, e un valore caricato non
va mai perso — al più viene sovrascritto da una `OUT` successiva prima di essere
pubblicato, che è precisamente la semantica voluta ("due `OUT` sono due
caricamenti, non due messaggi").

> **Storia del progetto, utile da raccontare.** Nella prima versione questo caso
> era coperto da un bit `pending` sul produttore, che ricordava "una `OUT` è
> atterrata" e faceva fallire la `SETRDY` in sua assenza. Funzionava, ma era
> **stato di protocollo in più** oltre ai due contatori. Spostando il gate dalla
> `OUT` alla cattura sul fronte il bit è diventato superfluo. E si è scoperto
> che era anche una pessimizzazione: costringeva il produttore a un giro di
> ritentativo in un caso in cui la pubblicazione poteva passare subito — si veda
> §5.4, dove i cicli di `catena.csv` sono **scesi** togliendolo.

### 2.6 ISRDY e SETRDY viste dal programma

Le due istruzioni sono **entrambe test non bloccanti**: non sospendono il core,
restituiscono un esito in un registro. È il *programma* a decidere cosa fare, di
solito un respin.

| Istruzione | Guarda | Ritorna | Effetto collaterale nel simulatore |
|---|---|---|---|
| `ISRDY rd, dir` | `in_ch[dir]` (canale del vicino) | 1 se c'è un dato da leggere | se 0, incrementa `attese` |
| `SETRDY rd, dir` | `out_ch[dir]` (canale proprio) | 1 se ha pubblicato | se 0, incrementa `ritentativi` |

Implementazione: [risc.c:497-510](src/risc.c#L497-L510).

**Perché non bloccanti?** Perché un test bloccante è una primitiva di
sincronizzazione, e in hardware richiede o uno stallo della pipeline o uno
scheduler. Non bloccanti sono un semplice filo letto come un bit: il costo
implementativo è nullo e il costo in cicli lo paga esplicitamente il programma,
dove è visibile e misurabile. È anche il motivo per cui `attese` e `ritentativi`
sono numeri sensati da riportare: corrispondono a istruzioni realmente eseguite.

`IN` è **consume-on-read**: legge il dato e, se il canale era pieno, libera lo
slot (`rp_next = rp ^ 1`). Leggere un canale vuoto restituisce il dato vecchio e
non tocca lo stato — non è un errore, è responsabilità del programma aver fatto
`ISRDY` prima.

### 2.7 Il ciclo completo, passo per passo

Produttore in (0,0) che manda 42 a est, consumatore in (0,1):

| Ciclo | Produttore | Consumatore | Stato a fine commit |
|---|---|---|---|
| 0 | `OUT s1, EST` → `data_next = 42` | `ISRDY t0, OVEST` → **0** (`attese++`) | vuoto (`wp==rp`), `data` intatto |
| 1 | `SETRDY t0, EST` → **1**, `wp_next = wp^1` | `ISRDY t0, OVEST` → **0** (`attese++`) | **pieno**; `data` cattura 42 |
| 2 | (istruzione successiva) | `ISRDY t0, OVEST` → **1** | pieno |
| 3 | `OUT s1, EST` → `data_next = 43` | `IN t1, OVEST` → 42, `rp_next = rp^1` | **vuoto**; `data` resta 42 |
| 4 | `SETRDY t0, EST` → **1**, pubblica 43 | ... | pieno; `data` cattura 43 |

Le righe da guardare sono la 3 e la 4. Al ciclo 3 il produttore carica il valore
successivo **mentre** il consumatore sta leggendo il precedente: la `OUT` va a
segno lo stesso, ed è innocua perché tocca solo `data_next` — il consumatore
legge `data`, che resta 42 finché `wp` non commuta. Al ciclo 4 il canale si è
svuotato e la `SETRDY` consegna 43: il valore giusto, una volta sola, **senza un
giro di ritentativo**.

È esattamente il caso che nella prima versione costava un respin (§2.5) ed è il
motivo per cui i cicli sono scesi.

Questo caso — e tutti gli altri limite — sono asseriti uno per uno in
[tests/test_channel.c](tests/test_channel.c): doppia `SETRDY` senza lettura in
mezzo, `SETRDY` senza `OUT`, `OUT` a canale pieno seguita da lettura e
pubblicazione (il test di regressione della modifica), lettura a vuoto, due
pubblicazioni nello stesso ciclo.

### 2.8 Due dettagli che vale la pena saper difendere

**(a) La guardia `wp_next == wp` in `ch_write`.**
È l'**unica** guardia rimasta sulla `OUT`, e non c'entra con la backpressure: dice
che il canale accetta al massimo **una pubblicazione per ciclo**. Dopo il commit
vale sempre `wp_next == wp`, quindi `wp_next != wp` significa "pubblicazione di
questo ciclo già decisa" e il registro va congelato. Un RISC non ci arriva mai —
esegue una istruzione per ciclo, non può fare due `OUT`+`SETRDY` nello stesso — ma
**l'host che alimenta il bordo può chiamare `grid_push` due volte di fila**, e
senza la guardia la seconda spinta si sostituirebbe al dato di cui la prima ha già
promesso la consegna. È asserito in
[test_bordo.c:56-63](tests/test_bordo.c#L56-L63).

**(b) Al respin si riparte dalla `OUT`, e ora è una scelta, non un obbligo.**
Si vede in tutti i kernel, per esempio [matmul.s:81-83](asm/matmul.s#L81-L83):

```asm
3:  OUT     t0, EST
    SETRDY  t2, EST
    beqz    t2, 3b        # torna alla OUT
```

Siccome la `OUT` carica il registro **anche a canale pieno**, il valore resta lì e
ritentare la sola `SETRDY` basterebbe a consegnarlo. Ricaricare lo stesso valore a
ogni giro è idempotente e costa nulla, e tenere **una sola forma di loop in tutti
i `.s`** vale più della istruzione risparmiata.

Vale la pena saperlo perché nella prima versione era il contrario: lì la `OUT`
veniva rifiutata e ripartire dalla `SETRDY` avrebbe fatto girare la cella a vuoto
per sempre. Il fatto che ora quel vincolo sia sparito è la prova più diretta che
il bit di stato in meno ha semplificato anche il lato programma.

---

## 3. L'ISA: le quattro istruzioni di comunicazione

File: [asm/macros.s](asm/macros.s), implementazione nel `case PCIO` di
[risc.c:452-512](src/risc.c#L452-L512).

Si usa l'opcode **custom-0 = `0x0B`**, uno degli spazi lasciati liberi
dall'ISA RISC-V per le estensioni non standard. Formato **I-type**:

```
| imm[11:0] | rs1 | funct3 | rd | opcode |
|  31..20   |19.15| 14..12 |11.7|  6..0  |
```

- **direzione** nei due bit bassi dell'immediato (0=N, 1=E, 2=S, 3=O)
- **funct3** distingue le quattro operazioni: 0=`IN`, 1=`OUT`, 2=`ISRDY`, 3=`SETRDY`

Le macro sono scritte con la direttiva `.insn` dell'assemblatore GNU:

```asm
.macro ISRDY rd, dir
    .insn i 0x0B, 0x2, \rd, x0, \dir
.endm
```

**Perché `.insn` e non una patch all'assemblatore?** Perché `.insn` è il
meccanismo previsto da binutils proprio per gli opcode custom: si usa la
toolchain `riscv64-unknown-elf-as` **ufficiale e non modificata**, e i file `.s`
restano assemblabili da chiunque senza ricompilare i tool. La macro dà la stessa
leggibilità di una istruzione vera.

**Perché `rv32i` puro dappertutto?** Perché l'assemblatore rifiuti ciò che
l'interprete non implementa: è un controllo automatico, non una limitazione.
L'unica eccezione è `matmul.s`, che usa `mul` e alza `ARCH` a `rv32im` solo per
sé ([Makefile](Makefile), regola `build/matmul.o`) — senza concedere la M a tutti
gli altri.

L'interprete implementa il set RV32I completo usato dai kernel: `LOAD`/`STORE`
(con `lb/lh/lw/lbu/lhu`, `sb/sh/sw` e le loro estensioni di segno), `OP_IMM`,
`OP`, `BRANCH`, `JAL`/`JALR`, `LUI`/`AUIPC`, `ECALL` (che ferma il core). Un
opcode non implementato ferma il core con un messaggio, non fa finta di niente
([risc.c:519-523](src/risc.c#L519-L523)).

---

## 4. L'I/O sulla griglia

File: [src/grid.c](src/grid.c), [src/grid.h](src/grid.h).

### 4.1 Cablaggio e identità

`grid_init` costruisce la topologia collegando `out_ch` e `in_ch` **per
puntatore**: `b->in_ch[OVEST] = &a->out_ch[EST]`. Non c'è nessuna copia, nessuna
tabella di routing, nessun oggetto "rete": i due lati dello stesso canale sono
letteralmente la stessa struct.

Ogni cella nasce sapendo dove si trova ([grid.c:30-33](src/grid.c#L30-L33)):

```c
k -> regs[10] = r;      /* a0 = riga */
k -> regs[11] = c;      /* a1 = colonna */
k -> regs[12] = rows;   /* a2 = righe totali */
k -> regs[13] = cols;   /* a3 = colonne totali */
```

**Perché precaricare l'identità invece di ricavarla dall'id lineare?** Perché
passare da `id` a `(riga, colonna)` richiede una **divisione**, e rv32i non ha
istruzioni di divisione. Il core dovrebbe eseguire una routine software di
decine di istruzioni all'avvio, in ogni cella, solo per sapere dove si trova.
Nell'hardware reale la posizione è cablata, quindi precaricarla è anche il
modello più fedele.

Questo abilita l'idea centrale dei kernel: **un solo programma, caricato identico
in tutte le celle, che sceglie il proprio ruolo dalla posizione**
([main.c:115-119](src/main.c#L115-L119)).

### 4.2 Il bordo: l'host è il vicino che non c'è

Le celle di perimetro hanno direzioni che puntano fuori dalla griglia. La griglia
alloca `2*(rows+cols)` canali propri e li collega a quelle direzioni
([grid.c:57-66](src/grid.c#L57-L66)). Da lì in poi **l'host di simulazione fa
esattamente quello che farebbe un vicino**:

```c
int grid_push(Grid *grid, int r, int c, int dir, uint32_t v) {
    Channel *ch = grid_at(grid, r, c) -> in_ch[dir];
    ch_write(ch, v);           /* la OUT che farebbe il vicino */
    return ch_setrdy(ch);      /* la sua SETRDY, con lo stesso esito */
}

int grid_pop(Grid *grid, int r, int c, int dir, uint32_t *v) {
    Channel *ch = &grid_at(grid, r, c) -> out_ch[dir];
    if (!ch_isrdy(ch)) return 0;   /* la ISRDY */
    *v = ch_read_c(ch);            /* la IN */
    return 1;
}
```

**Nessuna scorciatoia.** L'host non scrive `data` direttamente, non salta il
commit, non ignora il pieno/vuoto. Conseguenze:

- se la cella non ha ancora consumato il valore precedente, `grid_push`
  restituisce **0** — l'host subisce la **stessa backpressure** di una cella
  vicina, ed è un dato che viene misurato (colonna `spinte_rifiutate`);
- `grid_push`/`grid_pop` vanno chiamate **prima** di `grid_step`, nella stessa
  finestra in cui i core calcolano, così **anche l'host paga il ciclo di latenza
  per hop**. Se ne dà per acquisito lo scarto di un ciclo: i test raccolgono un
  ultimo valore *dopo* l'uscita dal loop, perché la pubblicazione finale viene
  committata nel giro in cui il core si ferma
  ([test_bordo.c:88-94](tests/test_bordo.c#L88-L94)).

`e_bordo` verifica che `(r,c,dir)` punti davvero fuori dalla griglia, e lo fa
**controllando se il puntatore cade dentro l'array `border`** — non ricalcolando
le coordinate. Il cablaggio ha già deciso, non serve rifare il conto.

### 4.3 Il drenaggio non è opzionale, l'alimentazione sì

Questa è l'asimmetria da spiegare bene:

- **senza alimentazione** i canali di bordo restano vuoti per sempre: è un bordo
  aperto da cui non arriva mai niente. Il kernel che aspetta lì gira a vuoto ma
  la griglia non si rompe;
- **senza drenaggio** un `OUT` di perimetro resta pieno per sempre, e la cella
  che vuole pubblicare lì si **inchioda sulla propria `SETRDY`**. È esattamente
  perché il protocollo è onesto: nessuno consuma, quindi nessuno può pubblicare.

Per questo `run_grid` chiama `grid_border_drain` **sempre**, mentre
`grid_border_fill` solo se è stata data la variabile `BORDO`
([main.c:63-74](src/main.c#L63-L74)). Il kernel `bordo.s` esiste apposta per
dimostrare questo punto.

### 4.4 Tre livelli di I/O, dal più povero al più ricco

| Livello | Cosa fa | Chi lo usa |
|---|---|---|
| `BORDO=n` (variabile d'ambiente) | costante uguale su tutto il perimetro, a ogni ciclo — la condizione di **Dirichlet** dello stencil | `make run P=jacobi BORDO=64` |
| `grid_border_fill` / `grid_border_drain` | le stesse cose chiamate da C | `test_jacobi.c` |
| `grid_push` / `grid_pop` per cella e per ciclo | contorno **non uniforme e variabile nel tempo**: un valore diverso per riga, per colonna e per giro | `test_matmul.c` — è l'unico caso, ed è il più significativo |

---

## 5. La raccolta dei dati

### 5.1 Chi conta cosa, e cosa significa esattamente

Ogni core tiene due contatori ([risc.h:28-29](src/risc.h#L28-L29)):

```c
uint32_t attese;        /* ISRDY che hanno trovato il canale di ingresso vuoto */
uint32_t ritentativi;   /* SETRDY rifiutate perché il canale di uscita era pieno */
```

`grid_spin` li somma su tutta la griglia. **Li conta il simulatore, non il
programma**: valgono quindi anche per i kernel che non tengono un contatore in
assembly.

> **La precisazione da fare subito, prima che la chieda:**
> `attese` e `ritentativi` contano **istruzioni fallite, non cicli persi**.
> Un giro di respin costa quanto è lungo il loop del kernel — in `prodcons.s`
> cinque istruzioni per ritentativo e quattro per attesa. Per il costo in cicli
> si guarda la colonna `cicli`, mai queste due.

E ancora: il **totale non dice dove** si aspetta. Per il collo di bottiglia serve
il dato per cella — `make test-broadcast` senza `CSV=1` stampa la mappa di `s4`.

### 5.2 Come sappiamo che i contatori sono giusti (taratura)

`prodcons.s` e `chain.s` tengono **lo stesso conto in assembly**, in `s3`
(SETRDY rifiutate) e `s4` (ISRDY a vuoto). `test_catena.c` somma i registri di
tutte le celle e **asserisce che coincidano con quelli del simulatore**:

```c
assert(ritentativi == reg_rit);
assert(attese == reg_att);
```
([test_catena.c:88-93](tests/test_catena.c#L88-L93))

Ogni giro di respin esegue esattamente una `ISRDY`/`SETRDY` fallita, quindi i due
numeri **devono** essere uguali. È la taratura: da lì viene la fiducia nei
contatori per tutti gli altri kernel, che non hanno un equivalente in assembly
con cui confrontarsi.

### 5.3 La pipeline `make dati`

1. ogni harness di test stampa **una riga CSV su stderr** per esecuzione (il
   risultato), mentre la traccia per istruzione va su `/dev/null`;
2. il Makefile scrive l'intestazione e concatena lo sweep;
3. `CSV=1` zittisce le stampe multiriga (mappa delle attese del broadcast,
   traccia dei delta di Jacobi) che spezzerebbero il formato.

```
make dati                        # rigenera tutti i CSV
column -t -s, docs/dati/*.csv    # per leggerli a occhio
```

**Nessun numero è scritto a mano.** Se un valore non torna si cambia il codice e
si rigenera. Le colonne sono documentate in
[docs/dati/README.md](docs/dati/README.md).

Gli sweep sono quelli dei test: `RITARDI = 0 1 4 8 25 60`,
`COLONNE = 2 3 5 12`, `FORME = 1x1 1x6 6x1 3x4 4x4 8x8 12x12`, `SEMI = 0 1`,
`K = 1 4 7`. Le forme `1x6` e `6x1` sono lì apposta: **isolano i due cicli di
cablaggio di `grid_init`** (est-ovest e nord-sud), che le forme quadrate
mescolerebbero.

### 5.4 Il risultato principale: `NOBP=1`, il termine di paragone

`NOBP=1` degrada il canale a **un registro senza handshake**: `OUT` sovrascrive
anche uno slot non ancora letto, `SETRDY` non fallisce mai, `ISRDY` dice sempre
di sì ([risc.c:476-487](src/risc.c#L476-L487)). Il doppio buffer resta, quindi
fra le due modalità **cambia solo il controllo di flusso**: latenza per hop e
ordine di visibilità sono identici. È il sistolico in *lockstep* puro che il
ready bit sostituisce.

Stesso kernel, stesso sweep, da `docs/dati/catena.csv`:

| RITARDO | backpressure | cicli | somma | attesa |
|---|---|---|---|---|
| 0 | sì | 45 | **15** | 15 |
| 0 | no | 37 | **15** | 15 |
| 1 | sì | 52 | **15** | 15 |
| 1 | no | 52 | **18** | 15 |
| 4 | sì | 82 | **15** | 15 |
| 4 | no | 82 | **22** | 15 |
| 8 | sì | 122 | **15** | 15 |
| 8 | no | 122 | **24** | 15 |
| 25 | sì | 292 | **15** | 15 |
| 25 | no | 292 | **25** | 15 |
| 60 | sì | 642 | **15** | 15 |
| 60 | no | 642 | **25** | 15 |

Come leggerlo, in tre frasi:

1. **Con il ready bit la somma è 15 sempre**, per ogni valore di `RITARDO` e —
   nelle righe `chain` — per ogni lunghezza della catena (2, 3, 5, 12 colonne).
   Il risultato non dipende dalla velocità relativa dei nodi né dalla topologia.
2. **Senza, si perdono dati in silenzio.** A `RITARDO=25` e `60` la somma è 25 =
   5+5+5+5+5: il consumatore è così lento che vede **cinque volte l'ultimo
   valore**. Nessun errore, nessun avviso, solo un risultato sbagliato.
3. **E non lo si paga in cicli.** Da `RITARDO=1` in poi le due modalità
   impiegano **esattamente lo stesso numero di cicli**. Il ready bit è gratis:
   il tempo lo consuma il consumatore lento, non il protocollo. Solo a
   `RITARDO=0` il lockstep risparmia 8 cicli su 45, ed è l'unico caso in cui
   per puro caso non perde nulla (il consumatore è più veloce del produttore).

`make test-nobp` è **volutamente fuori** da `make test`: lì il fallimento è il
risultato che si vuole leggere, non un guasto.

### 5.4-bis L'effetto misurato della rimozione di `pending`

Quando il bit è stato tolto (§2.5) tutti gli sweep sono stati rigenerati e
confrontati riga per riga con quelli di prima. Risultato:

| file | esito |
|---|---|
| `bordo.csv`, `broadcast.csv`, `jacobi.csv`, `matmul.csv`, `reduce.csv` | **identici, byte per byte** |
| `catena.csv` | cambiato, **sempre in meglio** |

| caso | cicli | ritentativi | attese |
|---|---|---|---|
| `prodcons` `RITARDO=0` | 49 → **45** | 2 → **1** | 3 → **2** |
| `chain` 1×12 `RITARDO=0` | 130 → **126** | 2 → **1** | 123 → **112** |
| `chain` 1×12 `RITARDO=60` | 643 → 643 | 114 → **113** | 99 → **90** |

**Tutte le somme restano 15** e la taratura `s3`/`s4` contro `grid_spin` regge
ancora: nessun dato perso o duplicato, solo meno attesa.

La spiegazione è quella della tabella di §2.7: `pending` costringeva a un giro di
respin nel caso "canale pieno alla `OUT`, vuoto alla `SETRDY` del ciclo dopo",
che ora passa al primo colpo.

E si spiega anche **perché solo `catena.csv`**:

- gli altri kernel hanno `ritentativi = 0`, cioè non imboccano mai quel percorso;
- `matmul` ha backpressure vera, ma **lato host**, e `grid_push` fa `ch_write` e
  `ch_setrdy` nella **stessa** chiamata, quindi nello stesso ciclo: il canale non
  può svuotarsi in mezzo e non c'è niente da guadagnare. Infatti
  `spinte_rifiutate` non si muove di una unità.

Il guadagno vive esattamente dove `OUT` e `SETRDY` distano un ciclo, cioè solo
dentro il RISC. È un buon segnale che la modifica sia stata capita: si può
prevedere in anticipo quali righe si sarebbero mosse.

### 5.5 Le altre misure notevoli

**`matmul.csv` — la scalabilità.** I dati seguono esattamente

```
cicli = 19 + 16(k−1) + 10(R+C−2)
```

Verificato su tutte le righe: k=1 su 1x1 → 19; k=7 su 1x1 → 115; k=1 su 12x12 →
239. **Il numero di celle non compare nella formula**: compaiono solo `k`
(lunghezza del prodotto interno) e `R+C−2` (il riempimento della pipeline, cioè
la distanza dell'ultima cella dall'angolo). Un array 12×12 fa 144 prodotti
interni nello stesso tempo in cui un 1×1 ne fa uno, più il riempimento. **È la
definizione di scalabilità di un array sistolico**, misurata invece che
affermata. Le tre costanti dipendono dalla lunghezza del loop di `matmul.s`
(16 istruzioni per giro, 3 fra prologo ed `ecall`): se cambia il kernel vanno
rimisurate.

**`jacobi.csv` — il costo è puro calcolo.** Su **tutte** le forme, da 1×1 a
12×12: `cicli = 4227`, `ritentativi = 0`, `attese = 0`. Il conto torna
esattamente: 33 istruzioni per iterazione × 128 iterazioni + 2 di prologo +
`ecall` = 4227. **Nessuno spin, mai.** Il kernel è perfettamente uniforme, tutte
le celle avanzano in fase, e ogni `ISRDY` trova il dato già pronto. È il caso in
cui il ready bit non costa **nulla** e serve solo come garanzia.

Inoltre `usciti = 2(R+C)·128` esattamente su ogni riga (12×12 → 48 canali di
bordo × 128 iterazioni = 6144): **niente perso, niente duplicato al perimetro**.

`delta = 0` ovunque significa che `k_finale` è una **convergenza vera** (punto
fisso raggiunto), non semplicemente l'ultima iterazione eseguita — e in quel caso
il test asserisce anche che il punto fisso sia la **soluzione esatta**, cioè 64
su tutta la griglia ([test_jacobi.c:151-156](tests/test_jacobi.c#L151-L156)).
`k_finale` cresce con la griglia (1×1 → 1, 4×4 → 18, 8×8 → 49, 12×12 → 82) ed è
la tabella che dice **con quale `ITER` compilare per una tolleranza data**: è il
`while err > tol` di MATLAB srotolato.

**La precisazione onesta sul 1×1.** La riga 1×1 non è un array che calcola: con
`rows = cols = 1` i due cicli di cablaggio dei vicini in `grid_init` non girano
nemmeno una volta, e **tutti e quattro** gli ingressi finiscono sui canali di
bordo ([grid.c:57-66](src/grid.c#L57-L66)). Quella cella legge 64 da tutte le
direzioni, calcola `(64·4+2)>>2 = 64` e converge in una iterazione — il
`k_finale = 1` della tabella. È una cella e l'host, e l'host fa il 100 % della
comunicazione.

Quindi il confronto "1×1 contro 12×12 a parità di cicli" è il più sfavorevole
possibile per l'array, e va riformulato sul **rapporto perimetro/area**: gli
ingressi a carico dell'host sono `2(R+C)` su `4RC` totali.

| forma | canali di ingresso | di cui di bordo | quota a carico dell'host |
|---|---|---|---|
| 1×1 | 4 | 4 | **100 %** |
| 4×4 | 64 | 16 | 25 % |
| 8×8 | 256 | 32 | 12,5 % |
| 12×12 | 576 | 48 | **8,3 %** |

La quota vale `(R+C)/(2RC)` e **tende a zero**: più l'array è grande, più è
autosufficiente. L'argomento della scalabilità non si indebolisce, si rafforza —
ma va detto in questa forma.

Due conseguenze che il numero `cicli` non mostra:

1. **Il costo dell'host non entra in `cicli`.** `grid_border_fill` e
   `grid_border_drain` fanno `2(R+C)` push e altrettante pop per ciclo, ed è
   codice C fuori dall'array simulato. È difendibile perché cresce come
   **`O(R+C)`, non `O(R·C)`**, cioè più lentamente del lavoro utile — ma è
   lavoro reale che la colonna non conta.
2. **In hardware quel costo si chiama banda di I/O.** I pin stanno sul perimetro
   e crescono come `R+C`, le unità di calcolo come `R·C`: è il limite noto degli
   array sistolici, prima o poi il bordo non riesce più a tenere occupato
   l'interno. Nel simulatore non si vede perché l'host è infinitamente veloce, e
   infatti in Jacobi `ritentativi = 0`. Dove si intravede è **`matmul`**, l'unico
   kernel in cui l'host viene rifiutato davvero: `spinte_rifiutate` passa da 71
   (1×1, k=4) a 3252 (12×12, k=7). Detto così i dati coprono entrambi i regimi:
   Jacobi *compute-bound*, matmul *I/O-bound*.

**`bordo.csv` — la forma della griglia si legge nei numeri.** Con 6 celle:
`1x6` → 9 cicli, 6 attese; `6x1` → **39 cicli, 51 attese**. Sono le stesse sei
celle. In `1x6` sono sei colonne indipendenti alimentate tutte insieme; in `6x1`
sono una catena verticale in cui ogni cella deve aspettare quella sopra. I
contatori raccontano la topologia.

**`broadcast.csv` e `reduce.csv` — `ritentativi = 0` ovunque.** Nessuna
backpressure: in questi kernel ogni cella pubblica una volta sola e c'è sempre
qualcuno pronto a consumare. Le `attese` crescono con la forma perché
rispecchiano la **latenza di propagazione**, non una contesa.

---

## 6. I kernel

Otto programmi in `asm/`, ciascuno con il suo harness in `tests/`. Sono ordinati
per complessità crescente e ognuno dimostra **una** proprietà.

### `prodcons.s` — il protocollo minimo
Griglia 1×2: la colonna 0 manda 1..Q a est, la colonna 1 accumula. Due ruoli
puliti, scelti da `a1`. Il consumatore può essere rallentato di `RITARDO` cicli
per generare backpressure a comando. Tiene i contatori in `s3`/`s4` e serve da
**taratura** dei contatori del simulatore (§5.2).
*Verifica:* somma == Q(Q+1)/2 per ogni `RITARDO`.

### `chain.s` — indipendenza da lunghezza e velocità
Generalizza il precedente a 1×C: sorgente, N nodi di inoltro, pozzo. Con C=2
degenera esattamente in `prodcons.s`, ed è lo stesso harness a testarli entrambi.
I nodi di mezzo sanno quando fermarsi perché **Q è una costante di build nota a
tutti**: nessun valore sentinella nel flusso dati.
*Verifica:* somma == Q(Q+1)/2 su 6 `RITARDI` × 4 lunghezze = 24 combinazioni. La
catena cambia la latenza, **mai** il risultato.

### `broadcast.s` — `ISRDY` su due direzioni
Un valore parte da (0,0) e raggiunge ogni cella propagandosi come **onda
diagonale**: la cella (r,c) lo vede dopo r+c hop. Ogni cella non-sorgente non sa
da quale dei due predecessori arriverà per primo, quindi fa polling **alternato**
su nord e ovest e prende il primo che risponde ([broadcast.s:30-39](asm/broadcast.s#L30-L39)).
Dimostra che `ISRDY` non bloccante permette di aspettare **più eventi
contemporaneamente** senza uno scheduler.
*Verifica:* `s1 == VALORE` in **tutte** le R×C celle, su 7 forme.

### `bordo.s` — esiste solo per dimostrare l'I/O di bordo
Quattro istruzioni: aspetta da nord, ripubblica a sud. Entrambi i capi della
catena cadono **fuori** dalla griglia: la prima riga aspetta un vicino che non
esiste (lo alimenta l'host), l'ultima pubblica verso nessuno (lo drena l'host).
È il kernel che rende evidente perché `grid_pop` esiste e perché "leggere" il
canale non basta.
*Verifica:* esce **esattamente un valore per colonna**, né perso né duplicato; e
il valore è arrivato intatto in fondo a ogni colonna.

### `reduce.s` — asimmetria dell'algoritmo, non del bordo
Riduzione in due fasi: prima ogni riga si somma da ovest a est, poi l'ultima
colonna si somma da nord a sud. Il totale esce dal bordo sud-est. Il percorso è
lungo (C−1)+(R−1) hop invece dei `log` di un albero vero, ma **le celle possono
parlare solo con i quattro vicini: l'albero non è cablabile**.
Il contributo della cella (r,c) è `r+c`, così il totale atteso si **ricava dalla
forma** invece di essere scritto a mano — e una cella cablata al posto sbagliato
cambia il risultato.
Punto da sottolineare: qui il kernel **deve** chiedersi "sono la colonna 0?".
Quell'asimmetria è dell'**algoritmo**, non del bordo, e nessun supporto dell'host
la toglierà mai. È il contrasto con `jacobi.s`.
*Verifica:* i parziali di **ogni** cella, non solo il totale.

### `jacobi.s` — il primo kernel completamente uniforme
Stencil a 5 punti: ogni cella sostituisce il proprio valore con la media dei
quattro vicini, `ITER` volte. **Nessun salto condizionato sulla posizione,
nemmeno ai margini**: le celle di perimetro leggono la condizione al contorno dal
canale di bordo come leggerebbero un vicino, e pubblicano verso l'esterno come
pubblicherebbero verso un vicino. Tutte le celle eseguono **la stessa identica
sequenza di istruzioni**. È il risultato che l'astrazione del bordo doveva
rendere possibile.

Due cose da saper difendere:

**(a) Perché tutti e quattro gli `OUT` prima di tutti e quattro gli `IN`.**
Non è stile, è la **condizione di assenza di deadlock** su canali di profondità 1.
*Argomento:* supponiamo per assurdo che tutte le celle siano bloccate. Si prenda
quella all'iterazione più bassa. Non può essere in fase di **spedizione**, perché
aspetterebbe che qualcuno consumi, e chi consuma sarebbe a un'iterazione ancora
inferiore — contro la scelta del minimo. Quindi è in fase di **ricezione**. Ma
una cella in ricezione ha per costruzione **già pubblicato tutti e quattro** i
suoi valori; e i suoi vicini, essendo a iterazione ≥ della sua, hanno anch'essi
già pubblicato. Dunque ciò che aspetta **c'è già**, e non è bloccata.
Contraddizione. ∎
Ricevere prima di spedire va in deadlock al primo giro; alternare per direzione
si sblocca solo per come sono disposti i bordi — cioè per caso.

**(b) Perché l'arrotondamento e non `srai` secco.**
`srai s1, s2, 2` tronca, e la troncatura è un **bias sistematico verso il basso**
che crea un **punto fisso spurio**: quando i quattro vicini valgono B−1 la media
troncata resta B−1, e il campo si ferma **sotto** la soluzione per qualunque
numero di iterazioni (misurato su 4×4 con bordo 64: stallo a 60..62 anche a
ITER=64). Con `addi s2, s2, 2` prima dello shift — cioè l'arrotondamento al più
vicino — converge esatto a 64. La somma di quattro valori sta larga fino a
bordo ≈ 2²⁹, quindi non c'è rischio di overflow.
Questa è la ragione per cui il test asserisce che *se* il riferimento raggiunge
un punto fisso, quel punto fisso **deve** essere la soluzione vera: con `srai`
secco quell'asserzione fallisce.

*Verifica:* il riferimento in C rifà le **stesse identiche operazioni su int32_t**
(aritmetica intera deterministica, non un'approssimazione), quindi il confronto è
**esatto bit per bit** su ogni cella, e `max|u_k − u_{k−1}|` calcolato in C **è**
la convergenza dell'array.

### `matmul.s` — il ready bit al posto della tabella dei tempi
`C = A × B` su griglia R×C: la cella (i,j) accumula `C[i][j]` e **non si muove**.
A scorrere sono i **dati**: la riga i di A entra da ovest e attraversa verso est,
la colonna j di B entra da nord e scende verso sud. In K giri ogni cella vede
passare esattamente gli operandi del proprio prodotto interno, e nient'altro.

**Questo è il kernel che dimostra la tesi del progetto.** In un sistolico in
lockstep l'host **deve** sfalsare l'ingresso — `a[i][k]` al ciclo i+k, `b[k][j]`
al ciclo j+k — altrimenti la cella moltiplica la coppia sbagliata e il risultato
è **sbagliato in silenzio**. Qui l'host non sfalsa niente: fa `grid_push` appena
il canale accetta e ritenta quando viene rifiutato
([test_matmul.c:81-98](tests/test_matmul.c#L81-L98)). La cella si blocca finché
non ha **entrambi** gli operandi, e siccome ogni canale consegna in ordine, il
k-esimo valore letto da ovest **è** `A[i][k]`. La correttezza non dipende più dal
tempo.

Nota di simmetria con Jacobi: qui ricevere prima di spedire **non** va in
deadlock, perché ogni canale ha un verso solo (ovest e nord si leggono, est e sud
si scrivono) e **tutte le dipendenze vanno verso sud-est**: una catena di attese
non può chiudersi in cerchio, risale fino al bordo dove c'è l'host. In Jacobi lo
scambio è simmetrico e serve l'argomento di (a).

Per la stessa uniformità **l'host deve drenare est e sud**: nessuna cella sa di
stare sul bordo, quindi l'ultima colonna e l'ultima riga inoltrano comunque.

*Verifica:* ogni cella ha il **proprio** elemento di C (matrici non simmetriche
di proposito: con A[i][k]=i+k uno scambio righe/colonne nel cablaggio passerebbe
inosservato), `s2 == 0` su tutte (i K giri fatti tutti), e tutti i termini
consegnati.

### `memtest.s` — la RAM
Si autoverifica e lascia il verdetto in `s1`. Copre: `lw` rilegge ciò che `sw` ha
scritto; gli offset sono **byte, non parole**; i word adiacenti distano 4 byte;
`sb` cambia un solo byte dentro il word; `lh`/`lb` estendono il segno e
`lhu`/`lbu` no. L'ultimo controllo è un accesso a 16384, **fuori dai 16 KB**: il
core deve fermarsi lì, altrimenti sconfinerebbe nella cella successiva
dell'array flat — che è il bug più insidioso possibile in questa architettura,
perché sarebbe memoria condivisa non voluta.

---

## 7. Domande probabili, con la risposta pronta

**Perché profondità 1 e non una FIFO più profonda?**
Perché una FIFO nasconderebbe il problema invece di risolverlo: con abbastanza
buffer il produttore non si ferma quasi mai e il ready bit sembra inutile.
Profondità 1 è il caso **peggiore e più onesto**, quello in cui il controllo di
flusso deve funzionare a ogni singolo trasferimento. È anche ciò che si cabla
davvero fra due celle adiacenti in un array sistolico: un registro, non una
memoria. Aumentare la profondità è un'estensione ortogonale — `wp`/`rp`
diventerebbero contatori mod N invece che mod 2 e il resto del protocollo non
cambierebbe di una riga.

**Cosa succede se due celle fanno `SETRDY` sullo stesso canale nello stesso
ciclo?**
Non può succedere: ogni canale ha **un solo** proprietario dell'`out_ch` (il
cablaggio è per puntatore, `OUT[d]` del mittente è `IN[opp(d)]` del ricevente, e
il ricevente non può scriverci). L'unico caso di doppia pubblicazione nello
stesso ciclo lo può creare **l'host**, e la guardia `wp_next == wp` in `ch_write`
lo rifiuta esplicitamente — con tanto di asserzione in `test_bordo.c` e
`test_channel.c`.

**`attese` alto significa che il kernel è lento?**
No. `attese` conta **istruzioni `ISRDY` fallite**, non cicli persi, e cresce
naturalmente con la latenza di propagazione: in `broadcast` su 12×12 vale 4477
semplicemente perché la cella (11,11) deve aspettare 22 hop. Per il costo si
guarda `cicli`. `attese` serve per il **confronto relativo** fra celle: la
mappa per cella dice dov'è il collo di bottiglia, il totale no.

**Perché Jacobi impiega 4227 cicli su ogni forma, da 1×1 a 12×12?**
Perché il kernel è perfettamente uniforme e non fa **nessuno** spin
(`attese = ritentativi = 0` su tutte le righe). 4227 è il conto esatto delle
istruzioni: 33 per iterazione × 128 + 2 di prologo + `ecall`. Il tempo è
indipendente dalla dimensione della griglia — che è precisamente il motivo per
cui si costruisce un array.

**Ma in 1×1 chi alimenta i quattro lati? L'host. Allora il confronto vale?**
Domanda giusta, e la risposta è che vale **riformulato**. In 1×1 tutti e quattro
gli ingressi sono canali di bordo: è una cella e l'host, non un array. Il numero
solido non è "1×1 contro 12×12", è il **rapporto perimetro/area**: la quota di
ingressi a carico dell'host è `2(R+C)/4RC`, cioè 100 % su 1×1, 25 % su 4×4,
8,3 % su 12×12 — **tende a zero**. Più l'array cresce, più è autosufficiente, e
il costo dell'host cresce come `O(R+C)` contro `O(R·C)` di lavoro utile. In
hardware quel costo è la **banda di I/O sul perimetro**, che è il limite noto
degli array sistolici; nel simulatore non si vede perché l'host è infinitamente
veloce, ma nei dati c'è: è la colonna `spinte_rifiutate` di `matmul.csv`
(fino a 3252 su 12×12). Vedi §5.5 per la tabella completa.

**Perché `ritentativi = 0` in quasi tutti i kernel?**
Perché in `broadcast`, `reduce`, `bordo` e `jacobi` ogni cella pubblica su un
canale che qualcuno sta già aspettando: non c'è contesa, solo latenza. La
backpressure la si vede dove è stata **provocata di proposito**: in
`prodcons`/`chain` con il consumatore rallentato (fino a 141 ritentativi), e in
`matmul` dal lato host (`spinte_rifiutate`, fino a 3252). Zero non è un test che
non ha funzionato: è la misura che in quei kernel il canale non è mai il collo di
bottiglia.

**I contatori `wp`/`rp` non traboccano?**
No, sono `uint8_t` usati come contatori a **un bit**: l'unica operazione è
`x ^ 1`. Restano sempre in {0,1} e il confronto `wp != rp` è esatto per sempre.

**Cosa garantisce che non ci sia deadlock?**
Nulla lo garantisce a livello di protocollo — ed è corretto così, perché un
canale di profondità 1 con test non bloccanti **permette** al programmatore di
scrivere un kernel bloccante. La garanzia è **per kernel**, dimostrata:
per `jacobi.s` con l'argomento del minimo (§6), per `matmul.s` con l'aciclicità
delle dipendenze verso sud-est. Sul piano pratico ogni harness ha un **tetto sui
cicli** e `assert(cicli < MAX_CICLI)`: un deadlock introdotto da una modifica
fa fallire il test invece di appendere la macchina.

**Perché esiste `NOBP` se serve solo a rompere le cose?**
Perché senza un termine di paragone la tesi non è misurata, è affermata. `NOBP`
degrada **solo** il controllo di flusso e lascia intatto tutto il resto (doppio
buffer, latenza per hop, ordine di visibilità), quindi la differenza fra le due
colonne di `catena.csv` isola esattamente il contributo del ready bit. Il
risultato è che il ready bit costa **zero cicli** e compra la correttezza (§5.4).

**Il modello è sincrono, con un clock globale: quanto è realistico?**
È il modello di un circuito sincrono con un albero di clock unico, che è come
sono fatti gli array sistolici reali su un singolo chip. La cosa importante è che
**il ready bit non serve a compensare il disallineamento del clock** — serve a
compensare il disallineamento del **carico di lavoro**: due celle che eseguono
lo stesso programma possono trovarsi a punti diversi perché hanno preso rami
diversi o hanno aspettato dati diversi. `RITARDO` in `prodcons.s` modella
esattamente questo. Un modello asincrono vero (clock indipendenti) sarebbe
l'estensione naturale successiva e non richiederebbe di cambiare il protocollo,
solo il ciclo di `grid_step`.

**Perché la traccia per istruzione va su `/dev/null` nei test?**
Perché l'interprete stampa una riga per istruzione eseguita — utile per il debug
passo passo (`make step`, `STEP=1`), inutilizzabile su 4227 cicli × 144 celle. Il
risultato va su stderr in formato CSV, e i due flussi non si mescolano.

---

## 8. Come mostrare qualcosa dal vivo

```bash
make && make test                    # la suite completa, tutti gli assert
make test-nobp Q=5                   # le somme sbagliate: il risultato principale

make step P=prodcons R=1 C=2 N=40    # un ciclo per INVIO, stato di ogni cella
make run  P=jacobi R=4 C=4 N=4000 BORDO=64
make run  P=matmul ARCH=rv32im R=2 C=2 N=500

make dati && column -t -s, docs/dati/catena.csv
```

`make step` è la cosa migliore da far vedere: stampa registri e stato dei quattro
canali di **ogni** cella a ogni ciclo, con `*` per canale pieno e `.` per vuoto.
Il protocollo lo si vede muoversi.
