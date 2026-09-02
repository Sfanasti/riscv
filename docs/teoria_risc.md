# Teoria del progetto — Array di processori RISC_V con sincronizzazione NESO

> Documento di teoria: **cosa** stiamo costruendo, **come** e soprattutto **perché**.
> Compagno concettuale della specifica
> [spec_progetto_riscv_array.md](spec_progetto_riscv_array.md) e del racconto
> completo in [project_parte_teorica.md](project_parte_teorica.md), dove ogni
> punto qui accennato è ripreso con l'implementazione e le misure.

---

## 1. Cosa stiamo implementando (in una frase)

Estendiamo **stella** — un interprete (ISS, *Instruction Set Simulator*) RISC_V
scritto in C — perché, oltre a eseguire un singolo RISC, sappia simulare una
**griglia di RISC** che comunicano coi vicini cardinali (Nord/Est/Sud/Ovest)
scambiandosi dati con un **handshake produttore-consumatore** sicuro.

In concreto aggiungiamo **due nuove istruzioni** alla ISA, `ISRDY` e `SETRDY`,
che gestiscono la *segnalazione di "pronto"* tra RISC vicini.

---

## 2. Il punto di partenza: stella e RV32

Stella è già un interprete funzionante. Esegue il classico ciclo:

```
fetch  ->  decode  ->  execute     (in risc.c)
```

- **fetch**: legge la prossima istruzione dalla memoria all'indirizzo `pc`.
- **decode**: spacchetta i 32 bit dell'istruzione nei campi (`opcode`, `rd`,
  `funct3`, `rs1`, `rs2`, `funct7`, immediato).
- **execute**: uno `switch` sull'`opcode` applica l'effetto al register file.

Stella è a **32 bit**: registri e parole sono `uint32_t`, e il loader ELF
accetta solo binari a 32 bit. Il toolchain si chiama `riscv64-unknown-elf` ma
è solo il nome del pacchetto: con i flag `-march=rv32i -mabi=ilp32` genera
codice **rv32i puro**. Niente `_zicsr`: l'interprete non implementa istruzioni
CSR, quindi è l'assembler a rifiutarle prima che possano fermare un RISC in
esecuzione. Il vincolo è per-kernel, non globale: `matmul.s` è l'unico che usa
`mul` (estensione M) e alza `ARCH=rv32im` da solo (si veda il Makefile),
senza concedere la M a tutti gli altri.

> **Perché un ISS e non hardware?** Il progetto è tutto software. Il modello
> hardware (mesh/NoC) resta solo riferimento concettuale: niente FPGA, niente
> pacchetti, niente routing. La comunicazione è **registro-a-registro tra
> vicini diretti**.

---

## 3. Il modello architetturale: l'array

### Registri di ogni RISC
Oltre ai 32 registri standard `x0..x31` (con `x0` fisso a 0), ogni RISC ha
**8 registri di bordo**:
- 4 di uscita: `OUT[N]`, `OUT[E]`, `OUT[S]`, `OUT[W]`
- 4 di ingresso: `IN[N]`, `IN[E]`, `IN[S]`, `IN[W]`

### Il canale tra vicini
L'idea chiave: **il registro di uscita di un nodo verso una direzione _è_ il
registro di ingresso del vicino dalla direzione opposta.** Sono i due lati dello
stesso **canale**.

```
   (i,j) OUT[E]  ───────────►  IN[W] (i,j+1)
```

### Mappatura direzioni (FISSA, ovunque)
```
N = 0,  E = 1,  S = 2,  W = 3
opp(d) = d ^ 2        // N<->S (0^2=2),  E<->W (1^2=3)
```
Regola di accoppiamento: il **produttore** segnala con `SETRDY[d]`, il
**consumatore** controlla con `ISRDY[opp(d)]` sullo **stesso** canale.

> **Perché una mappatura unica?** Se produttore e consumatore usassero
> convenzioni diverse per le direzioni, i dati finirebbero sul canale sbagliato
> in modo silenzioso. Una sola tabella, usata da assembly e interprete, elimina
> un'intera classe di bug.

---

## 4. Le due nuove istruzioni: ISRDY e SETRDY

Sono **test non bloccanti** (non fermano mai il RISC):

- **`ISRDY rd, dir`** — *gate del consumatore*. Mette in `rd` il valore `1` se in
  `IN[dir]` c'è un dato leggibile, altrimenti `0`.
- **`SETRDY rd, dir`** — *gate del produttore*. Pubblica il dato già scritto in
  `OUT[dir]`: se lo slot era libero (il dato precedente è stato consumato) marca
  il canale come pieno e mette `1` in `rd`; se era ancora pieno **non**
  sovrascrive e mette `0`.

> **Perché non bloccanti?** È la scelta di design più importante. Se le
> istruzioni bloccassero in attesa, la logica di sincronizzazione sarebbe
> nascosta dentro l'interprete C. Rendendole semplici test che ritornano un
> esito, **i loop di attesa si scrivono in assembly RISC_V** — ed è lì che vive
> il vero comportamento del sistema (vedi §7). L'interprete resta minimale; il
> protocollo è esplicito nel codice del programmatore.

> **Perché `SETRDY` ritorna un esito?** Per essere **simmetrica** a `ISRDY`. Il
> produttore può così controllare se la pubblicazione è andata a buon fine prima
> di sovrascrivere. È quello che chiude il cerchio del produttore-consumatore.

### Rapporto con IN/OUT (già in stella)
Stella implementa già due istruzioni custom, `IN` e `OUT`, che spostano il
**dato** (leggono/scrivono i registri di bordo). `ISRDY`/`SETRDY` gestiscono solo
il **bit di pronto**. Insieme formano il quadro completo:

| Istruzione | Ruolo | funct3 |
|-----------|-------|:---:|
| `IN  rd, dir`  | dato: `rd <- IN[dir]` (e consuma) | 0 |
| `OUT rs, dir`  | dato: `OUT[dir] <- rs`            | 1 |
| `ISRDY  rd, dir` | ready: c'è un dato da leggere?  | 2 |
| `SETRDY rd, dir` | ready: pubblica il dato         | 3 |

> **Perché tenere IN/OUT?** Lo spec lasciava aperta la domanda "come si spostano
> i dati?" (proponeva i CSR). La stella che estendiamo ha già risposto con
> `IN`/`OUT`: quindi **non servono i CSR**. Riusiamo il percorso dati esistente e
> aggiungiamo solo la segnalazione. Meno codice nuovo, stesso risultato.

---

## 5. Come si codifica una istruzione "nuova" in RISC_V (task 1.a)

Una ISA ha **opcode liberi** riservati a estensioni custom. Usiamo **custom-0**:

```
opcode = 0b0001011 = 0x0B
```

Formato **I-type** (lo stesso di `ADDI`):
```
| imm[11:0] | rs1  | funct3 | rd  | opcode |
|  31..20   |19..15| 14..12 |11..7|  6..0  |
```
- `opcode` = `0x0B` distingue "famiglia custom"
- `funct3` distingue le quattro istruzioni (0=IN, 1=OUT, 2=ISRDY, 3=SETRDY)
- `dir` (0..3) sta nei **2 bit bassi dell'immediato**
- `rd` = registro dell'esito/dato; `rs1` = sorgente dato (solo per OUT)

> **Perché funct3 2 e 3, e non 0 e 1 come nello spec?** Perché 0 e 1 sono già
> occupati da `IN`/`OUT`. La regola d'oro: l'unico vincolo è che **l'assembler e
> il decoder usino lo stesso schema**. Lo decidiamo noi, quindi basta essere
> coerenti.

### Emetterle nei file `.s` senza patchare l'assembler
GNU `as` permette di iniettare istruzioni arbitrarie con la direttiva **`.insn`**
(NB: `.insn`, non `.inst` che è ARM). Le incapsuliamo in macro — vedi
[macros.s](macros.s):

```asm
.macro ISRDY rd, dir
    .insn i 0x0B, 0x2, \rd, x0, \dir
.endm
.macro SETRDY rd, dir
    .insn i 0x0B, 0x3, \rd, x0, \dir
.endm
```

Alternativa sempre valida: `.word 0x........` con l'encoding precalcolato.

### Verifica dell'encoding (fatta)
Verificato assemblando un `.s` con le quattro macro e disassemblando con
`riscv64-unknown-elf-objdump -d`: l'encoding combacia con quello calcolato a
mano. Il file usato allo scopo (`test_isa.s`) era un harness a perdere,
rimosso una volta confermato lo schema — la verifica che conta oggi è
`make test`, che esegue le istruzioni vere sui kernel veri invece di limitarsi
a controllarne la codifica.

---

## 6. Il protocollo di sincronizzazione (il cuore, spec §4)

Handshake **produttore-consumatore completo, profondità 1, a 1 bit**. Lo stato
di un canale è dato da due contatori mod 2, `wp` (write pointer) e `rp` (read
pointer):

```
pieno / leggibile   :  wp != rp
vuoto / scrivibile  :  wp == rp
```

Le quattro operazioni sul canale:
- **Scrittura dato** in `OUT[d]`: scrive solo il valore, non tocca i bit.
- **`SETRDY d`** (produttore): se `wp == rp` → `wp ^= 1` (ora pieno), ritorna 1;
  altrimenti ritorna 0 (slot occupato, non sovrascrivere).
- **`ISRDY opp(d)`** (consumatore): ritorna `(wp != rp) ? 1 : 0`.
- **Lettura dato** da `IN[opp(d)]` (consume-on-read): ritorna il valore e, se il
  canale era pieno, `rp ^= 1` (libera lo slot).

> **Perché chiude il cerchio?** Il produttore non riscrive finché il consumatore
> non ha letto (`SETRDY` fallisce se pieno); il consumatore non legge finché il
> produttore non ha pubblicato (`ISRDY` è 0 se vuoto). **Nessun dato perso, nessuna
> race condition.** Questo è il "punto su cui non transigere" #1 dello spec: senza
> il controllo dell'esito di `SETRDY` e senza il consume in lettura, un dato verrebbe
> sovrascritto o riletto.

**Correzione, arrivata dall'implementazione:** il modello sopra ha un buco.
`OUT` e `SETRDY` sono due istruzioni in due cicli distinti, e nella finestra
fra l'una e l'altra lo stato del canale può già essere cambiato — il
consumatore può aver letto `data` fra la `OUT` e la `SETRDY` che dovrebbe
pubblicarlo, per cui a `SETRDY` il canale risulta di nuovo "vuoto" e il dato
appena scritto viene ripubblicato al posto del prossimo. Misurato: la somma
1+2+3+4+5 usciva 19 anziché 15. Il `wp`/`rp` da solo non distingue "vuoto
perché non ho ancora scritto" da "vuoto perché il consumatore ha già letto
quello che stavo per pubblicare".

**Primo rimedio, poi scartato.** La risposta immediata è stata un terzo bit,
`pending`, che ricordasse da un ciclo all'altro che una `OUT` era stata accettata
ma non ancora resa visibile: `SETRDY` pubblicava solo se `pending` era alto — non
più "il canale è libero?" ma "ho davvero un dato nuovo da pubblicare?". La somma
tornava a 15, ma al prezzo di **stato di protocollo oltre i due contatori**, che
in revisione è stato respinto: il canale deve essere un sistema a transizione di
livello, contatori mod 2 e comparatori, nient'altro.

**Rimedio definitivo: spostare il gate invece di aggiungere memoria.** Il buco
esiste perché la `OUT` viene *rifiutata* quando il canale è pieno, e quindi al
ciclo dopo non c'è nulla di nuovo da pubblicare. Ma la `OUT` non ha motivo di
essere rifiutata: scrive `data_next`, che il consumatore non vede mai. Se la si
lascia sempre passare, il valore nuovo è già nel registro quando la `SETRDY`
riesce, e il duplicato non può nascere.

Perché il valore già pubblicato non venga cancellato da quella `OUT`, il dato
visibile si aggiorna **solo sulla transizione di `wp`**:

```c
if (wp_next != wp) { data = data_next; }   // enable = il segnale di pubblicazione
wp = wp_next;
rp = rp_next;
```

Sono due registri in serie — uno di uscita, uno visibile — con l'enable del
secondo pilotato dal toggle del contatore. Zero bit di stato in più.

Struttura dati C (`channel.h`), a doppio buffer sia sul dato sia sui bit di
controllo — per lo stesso motivo del two-phase update del §8, esteso qui a
tutto ciò che il canale espone:
```c
typedef struct Channel {
    uint32_t data;              // visibile: catturato alla transizione di wp
    uint8_t  wp, rp;            // write / read pointer (mod 2), stato attuale
    uint32_t data_next;         // registro di uscita: lo carica OUT
    uint8_t  wp_next, rp_next;  // stato prossimo, applicato al commit
} Channel;
```

Effetto collaterale gradito: `pending` non era solo stato in più, era anche una
pessimizzazione. Costringeva il produttore a un giro di respin proprio nel caso
in cui la pubblicazione poteva passare subito, e toglierlo ha fatto **scendere**
i cicli di `catena.csv` (`prodcons` a `RITARDO=0`: 49 → 45) lasciando identici
tutti gli altri sweep.

---

## 7. Dove vive davvero la logica: l'assembly

Poiché le istruzioni sono non bloccanti, i **loop di attesa** sono codice
RISC_V che scriviamo noi:

Produttore — manda verso Est:
```asm
    # ... scrivi il dato in OUT[E] con OUT ...
spin_p:
    SETRDY a0, EST       # a0 = 1 se pubblicato, 0 se slot ancora pieno
    beqz   a0, spin_p    # riprova finché il vicino non ha consumato
```
Consumatore — riceve da Ovest:
```asm
spin_c:
    ISRDY a0, OVEST      # a0 = 1 se c'è un dato
    beqz  a0, spin_c
    IN    a1, OVEST      # leggi: la lettura consuma e libera lo slot
```

---

## 8. L'interprete main e il TWO-PHASE UPDATE (spec §5)

Il driver dell'array fa avanzare tutti i RISC di N cicli. Per **non dipendere
dall'ordine** in cui scandisce i RISC, ogni ciclo è diviso in due fasi:

1. **Fase compute**: tutti i RISC eseguono un passo leggendo lo stato dei canali
   **committato a inizio ciclo**; le modifiche a `wp`/`rp`/`data` vanno in un
   buffer "next", non subito sullo stato visibile.
2. **Fase commit**: si applicano in blocco tutti gli aggiornamenti.

> **Perché two-phase?** Senza, il risultato dipenderebbe dall'ordine di scansione
> (il RISC 0 vedrebbe già gli effetti del proprio vicino nello stesso ciclo) e la
> simulazione non sarebbe **riproducibile**. Conseguenza voluta: la comunicazione
> ha **latenza 1 ciclo per hop**, esattamente come un array systolic reale. È
> fedeltà temporale: la *correttezza* del dato è già garantita dall'handshake.

> **Nota sulla stella della tesi:** il suo `run_multi_core` usa uno "snapshot"
> che in pratica è codice morto (copia `in_reg` su se stesso) e non implementa il
> two-phase vero. Riscritto: è `grid_step` + `ch_commit` (`channel.h`), non più
> uno snapshot ma il buffer `_next` per ogni campo, applicato in blocco.

### Registri di bordo
I nodi sul bordo della griglia hanno direzioni "verso l'esterno": le gestisce
l'**host** (l'interprete main), che tra un ciclo e l'altro scrive gli `IN` di
bordo (input nell'array) e legge gli `OUT` di bordo (output dall'array). Bordi
**aperti** (no wrap-around) salvo diversa indicazione.

> **Implementato in Fase 5** come `grid_push`/`grid_pop` (e
> `grid_border_fill` per un contorno costante), vedi
> [project_parte_teorica.md](project_parte_teorica.md) sezione 11. Due
> precisazioni che solo l'implementazione ha reso evidenti: "tra un ciclo e
> l'altro" va inteso come *prima* di `grid_step`, così l'host paga la stessa
> latenza di un ciclo per hop di qualunque cella invece di godere di una
> finestra temporale privilegiata; e "legge gli `OUT`" richiede di
> **consumarli**, perché col consume-on-read un output di perimetro guardato
> ma non letto resta pieno e blocca la cella sulla propria `SETRDY`.

---

## 9. Il caso d'uso dimostrativo: moltiplicazione di matrici systolic

`matmul.s` (implementato e testato) è un **array systolic** per `C = A × B`,
output-stationary: ogni cella (i,j) accumula `C[i][j]` in un registro, `A`
scorre verso Est, `B` scorre verso Sud, un *multiply-accumulate* per ciclo. È
l'applicazione tipica di questa architettura (tipo TPU). Usa il meccanismo
produttore-consumatore (self-timed, con ready bit) invece della tempistica
systolic in lockstep: scelta più generale e robusta, e con un vantaggio
misurabile.

> **Perché il ready bit conta qui più che altrove.** Un array systolic in
> lockstep classico richiede uno *skew*: l'host deve far entrare `a[i][k]`
> esattamente al ciclo `i+k`, altrimenti due operandi che non si corrispondono
> si incontrano nella stessa cella. Col ready bit questo calcolo non serve: la
> cella non avanza finché non ha ricevuto entrambi gli operandi, quindi
> l'accoppiamento corretto è garantito dal protocollo, non dal timing con cui
> l'host alimenta il bordo. È l'argomento speculare alla misura `NOBP` (si veda
> `project_parte_teorica.md`, dove il canale degrada a registro senza
> handshake): lì si vede cosa si perde senza ready bit, qui cosa si guadagna
> non dovendo più calcolare uno skew.

Non è l'unico kernel: la griglia è stata esercitata anche da `broadcast.s`,
`bordo.s`, `reduce.s` e dallo stencil di Jacobi (`jacobi.s`) — quest'ultimo
perché `D` (il grado di ogni nodo) è diagonale, la matrice del sistema è
esattamente il cablaggio a 4 vicini della griglia, e non va mai
materializzata: si veda `project_parte_teorica.md` per il dettaglio.

---

## 10. La controparte hardware: il modello in SystemVerilog

Il protocollo è nato come modello C, ma il suo scopo è descrivere un circuito.
`hw/` lo scrive in SystemVerilog (`neso_channel.sv` per il canale,
`neso_if.sv` per l'interfaccia completa di una cella) e serve a rispondere a una
domanda che il C non può: **quanto costa davvero questa interfaccia.**

### 10.1 Cosa sparisce passando a RTL

`data_next`, `wp_next`, `rp_next` e `ch_commit()` **non esistono** nel Verilog.
Il doppio buffer che in C va emulato a mano per dare la latenza di un ciclo per
hop è la semantica nativa di `<=` dentro un `always_ff`: tutti i registri
campionano il valore vecchio e cambiano insieme sul fronte. In hardware la
fedeltà systolic è gratis, in C costava tre campi in più (§8).

Sparisce anche la guardia `wp_next == wp` di `ch_write`. Se `wr_en` e `pub_ok`
sono alti nello stesso ciclo, `pub_reg <= out_reg` campiona il valore vecchio e
`out_reg <= wr_data` carica il nuovo: il non-blocking lo dà gratis. Quella
guardia serviva perché `grid_push` poteva chiamare `ch_write` due volte nello
stesso ciclo *simulato*, cosa che un ingresso campionato sul fronte non permette
per costruzione. Era un problema dell'emulazione, non del protocollo.

### 10.2 Cosa invece costa più che in C

I registri dati sono **due**, non uno. Siccome la `OUT` non è più rifiutata a
canale pieno — è così che si è potuto togliere il bit `pending` (§6) — il valore
appena caricato e quello pubblicato-e-non-ancora-letto possono essere diversi
nello stesso istante, e non possono condividere gli stessi flip-flop. In C era
gratis perché `data_next` esisteva già per altri motivi.

Numeri **misurati** con `yosys` (`make -C hw stat`), non stimati:

| | per canale | per cella (×4) | griglia 12×12 |
| --- | --- | --- | --- |
| flip-flop | **66** (32 `out_reg` + 32 `pub_reg` + `wp` + `rp`) | 264 | 38.016 |
| porte combinatorie | **6** (1 XOR + 1 XNOR + 4 di enable) | 24 | 3.456 |
| fili verso il vicino | 33 (32 dato + `isrdy`) | 132 | — |
| fili dal vicino | 1 (strobe di consumo) | 4 | — |

Fra due celle adiacenti corrono quindi **68 fili**, 34 per verso. E l'intero
protocollo di sincronizzazione — quello su cui è costruito tutto il progetto —
sta in **66 flip-flop e 6 porte**: XOR e XNOR *sono* i due comparatori
`isrdy`/`iswrt`, il resto sono gli enable.

`neso_if` non aggiunge nulla di sequenziale: il decode è tutto `always_comb`,
quindi i suoi 264 flip-flop sono esattamente i quattro canali.

### 10.3 L'asimmetria fra i due test, letta in RTL

Il decode di `neso_if.sv` rende visibile perché `SETRDY` può essere rifiutata e
`ISRDY` no:

| operazione | alza un enable? | cosa restituisce |
| --- | --- | --- |
| `ISRDY` | **no** | un filo, l'uscita del comparatore |
| `SETRDY` | sì | lo stesso filo che pilota l'enable — è un *test-and-set* |
| `IN` | sì | 32 bit di dato, che occupano tutto `op_result` |
| `OUT` | sì | niente (`rd = x0`) |

`ISRDY` non pilota nessun enable, quindi non c'è niente che possa essere
rifiutato: può valere 0, ma zero è una risposta, non un rifiuto. E `IN` occupa
tutti i 32 bit del risultato col dato letto — **per questo il suo test deve
stare in una istruzione separata**, mentre per il produttore test e azione
stanno in una sola: una istruzione RISC_V scrive un solo registro.

### 10.4 Cosa è verificato

`make -C hw` esegue due testbench: `tb_neso_channel.sv` ripercorre gli stessi
casi limite di `tests/test_channel.c` (compreso quello di regressione: una
`SETRDY` senza `OUT` davanti consegna il valore caricato a canale pieno, non un
duplicato), `tb_neso_if.sv` cabla due celle in 1×2 e ci fa passare la sequenza
di `prodcons.s`. `make -C hw lint` passa il codice a `verilator -Wall`, che è
più severo di iverilog.

---

## 11. Il parallelismo del modello: dove paga e dove no

Il simulatore fa girare `R×C` core che, per costruzione, non condividono
memoria. Sembra il caso ideale per il parallelismo, e in parte lo è — ma le
misure dicono anche dove *non* conviene, e quella è la parte che vale la pena
scrivere.

### 11.1 Perché è parallelizzabile senza un solo lock

`grid_step` separa compute e commit (§8) per la **riproducibilità**: senza quella
separazione il risultato dipenderebbe dall'ordine di scansione dell'array. Il
fatto che sia anche esattamente ciò che serve per parallelizzare è un guadagno
non cercato.

In fase compute ogni campo ha **un solo scrittore**:

| campo | unico scrittore |
| --- | --- |
| `data_next`, `wp_next` | il produttore del canale |
| `rp_next` | il consumatore, unico per costruzione |
| `regs`, `pc`, `memory`, `attese`, `ritentativi` | la cella proprietaria |

Il caso che sembra una race e non lo è: la cella `i`, leggendo da un vicino,
scrive `rp_next` **dentro la struct di quel vicino**. Due thread lavorano quindi
sulla stessa `Channel`, ma su campi diversi — e in C11 membri distinti di una
struct sono *memory location* distinte, quindi non c'è data race nemmeno fra i
due `uint8_t` adiacenti. È una cosa diversa dal *false sharing* di §11.4, che
riguarda le prestazioni e non la correttezza.

Le letture (`data`, `wp`, `rp` correnti) toccano campi che cambiano solo in fase
commit. Bastano quindi due direttive:

```c
#pragma omp parallel for schedule(static)
for (int i = 0; i < n; i++) {                /* fase compute */
    if (grid -> risc[i].running) {
        execute_step(&grid -> risc[i]);
    }
}
/* barriera implicita a fine loop */

#pragma omp parallel for schedule(static)
for (int i = 0; i < n; i++) {                /* fase commit */
    for (int d = 0; d < 4; d++) {
        ch_commit(&grid -> risc[i].out_ch[d]);
    }
}
```

`schedule(static)` perché ogni iterazione costa uguale — una istruzione simulata
— quindi distribuire a runtime aggiungerebbe solo overhead. Nessun `critical`,
`atomic` o `reduction`: **non servono, ed è questo il punto**.

La barriera implicita a fine `omp for` *è* la separazione delle due fasi:
nessun thread committa finché non hanno finito tutti di calcolare. Aggiungere
`nowait` la romperebbe, e con essa il determinismo. Il commit dei canali di
bordo resta seriale: sono `2(R+C)` contro `R·C`, il 3 % del lavoro su 128×128.

### 11.2 Prima misura: il parallelismo non è la leva principale

Su 64×64, 17,3 milioni di istruzioni simulate:

| variante | tempo | ns per istruzione |
| --- | --- | --- |
| com'era | 2,12 s | 122 |
| senza la traccia per istruzione | 0,90 s | 52 |
| senza traccia, con `-O2` | **0,61 s** | **35** |

I ~60 `printf` di `execute()` erano il **58 %** del tempo, e il Makefile non
passava alcun flag di ottimizzazione: **3,5× seriali prima di toccare il
parallelismo**. Non è un dettaglio preliminare — `printf` prende il lock di
`stdout` anche scrivendo verso `/dev/null`, quindi con la traccia accesa una
misura di parallelismo misurerebbe la contesa su quel lock, non il parallelismo.

### 11.3 Seconda misura: sotto ~24×24 l'OpenMP peggiora le cose

Costo di una regione parallela, misurato sulla macchina di sviluppo (20 core):
3,87 µs con 2 thread, 8,72 con 8, 13,89 con 16. `grid_step` ne apre **due** per
ciclo, contro un lavoro di 35 ns × celle:

| griglia | lavoro/ciclo | 8 thread, 2 regioni | atteso |
| --- | --- | --- | --- |
| 12×12 | 5,0 µs | 18,1 µs | **0,3×** |
| 32×32 | 35,8 µs | 21,9 µs | 1,6× |
| 64×64 | 143 µs | 35,4 µs | 4,1× |
| 128×128 | 573 µs | 89,1 µs | 6,4× |

Il crossover sta intorno a **24×24**, mentre le `FORME` dei test arrivano a
12×12: tutte le griglie su cui il progetto lavora oggi stanno dalla parte
sbagliata. Il risultato da riportare non è "va N volte più veloce", è **dove**
il parallelismo comincia a pagare — una tabella che ammette lo 0,3× su 12×12
dice più di una che mostra solo il caso buono.

Il rimedio, se servisse guadagnare sulle griglie piccole, è spostare la regione
parallela **fuori** dal loop dei cicli: si pagherebbero solo due barriere invece
di altrettanti fork/join. Costa però la ristrutturazione del driver, perché
l'I/O di bordo dell'host e il controllo di terminazione andrebbero in
`#pragma omp single`.

### 11.4 I due limiti veri: false sharing e Amdahl

`sizeof(Channel)` è 16 byte, quindi `out_ch[4]` occupa **esattamente 64 byte:
una linea di cache**. Su quella linea scrivono, a ogni ciclo, fino a **cinque**
thread: il proprietario (`wp_next` e `data_next` dei suoi quattro canali) e i
quattro vicini (`rp_next`). È false sharing da manuale, e le proiezioni di §11.3
non ne tengono conto: lo scaling reale sarà peggiore.

Il rimedio è spostare `rp`/`rp_next` nella cella consumatrice, così ogni linea
ha un solo scrittore. Vale la pena notare che è **lo stesso refactor che il
modello hardware suggerisce** (§10.2): fisicamente il flip-flop `rp` starebbe
nel consumatore, e tenerlo dov'è è una scelta di fedeltà a `channel.h`. Due
ragioni indipendenti — una di prestazioni, una di realismo hardware — che
indicano la stessa modifica.

L'altro tetto è Amdahl. Non tutto il ciclo si parallelizza: l'I/O di bordo
dell'host è `O(R+C)`, e il controllo di terminazione negli harness è un loop
**seriale `O(n)`** eseguito a ogni ciclo (`tests/test_jacobi.c`, il
`vivi |= running` su tutte le celle). Cronometrando separatamente il tempo
dentro `grid_step` e quello totale si ottiene `1 - t_step/t_tot`, cioè la
frazione seriale **misurata** invece che supposta — ed è il numero che spiega
perché lo speedup si ferma prima del numero di core.

### 11.5 Le griglie grandi e il muro della memoria

Le griglie dove il parallelismo pagherebbe davvero sono anche quelle che non
entrano in RAM. `sizeof(RISC_V)` è 16.632 byte, di cui 16.384 sono `MEM_SIZE`
(4096 parole):

| `MEM_SIZE` | `sizeof(RISC_V)` | griglia 1000×1000 |
| --- | --- | --- |
| 4096 parole (attuale) | 16.632 B | **15,5 GiB** |
| 1024 parole | 4.344 B | 4,1 GiB |
| 256 parole | 1.272 B | 1,2 GiB |

Una 1000×1000 è quindi **irraggiungibile** allo stato attuale. Rendere
`MEM_SIZE` un parametro non sblocca solo le griglie grandi: migliora anche il
caso seriale, perché lo stride fra celle contigue crolla da 16 KB a poco più di
1 KB e molte più celle entrano nelle cache. I kernel usano poche centinaia di
byte; l'unico vincolo è `memtest.s`, che ha il limite dei 16 KB scritto dentro e
andrebbe allineato con un `--defsym`.

### 11.6 Come si verifica che sia ancora corretto

Il commit a due fasi rende il risultato parallelo **bit-identico** al seriale,
quindi la verifica è la stessa già usata per la rimozione del bit `pending`:
rigenerare `docs/dati/` con 1, 2, 8 e 16 thread e pretendere che il `diff`
rimanga vuoto. Se un CSV si muove non è un problema di prestazioni, è il
determinismo rotto — e il risultato non deve dipendere dal numero di thread.
Una passata con `-fsanitize=thread` conferma l'analisi di §11.1 invece di
lasciarla sulla carta.

---

## 12. Riepilogo: cosa, come, perché

| Cosa | Come | Perché |
|------|------|--------|
| 2 istruzioni `ISRDY`/`SETRDY` | opcode `0x0B`, I-type, funct3 2/3, via `.insn` | segnalare "pronto" senza patchare l'assembler |
| handshake produttore-consumatore | `Channel` con `wp`/`rp` mod 2, profondità 1 | nessun dato perso, nessuna race |
| cattura del dato sulla transizione di `wp` | `if (wp_next != wp) data = data_next` nel commit, e la `OUT` non viene mai rifiutata | `OUT`/`SETRDY` sono 2 istruzioni in 2 cicli: senza, un dato letto in mezzo veniva ripubblicato. Risolto senza bit di stato in più |
| istruzioni non bloccanti | ritornano un esito; spin-loop in assembly | la logica vive nell'assembly, l'interprete resta minimale |
| two-phase update nel driver | leggi stato committato, applica i toggle a fine ciclo | riproducibilità + latenza 1 ciclo/hop come un array systolic |
| una sola mappatura direzioni | `N=0 E=1 S=2 W=3`, `opp=d^2` | elimina i bug silenziosi di accoppiamento |
| I/O di bordo dell'host | `grid_push`/`grid_pop` su `grid_at(r,c)->in_ch[dir]`, prima di `grid_step` | l'host è un vicino in più ai margini, non un canale privilegiato |
| modello hardware | `hw/neso_channel.sv` e `hw/neso_if.sv`, verificati con iverilog/verilator e sintetizzati con yosys | il protocollo nasce per descrivere un circuito: 66 flip-flop e 6 porte per canale, misurati (§10) |
| parallelismo del driver | due `#pragma omp parallel for` in `grid_step`, nessun lock | il commit a due fasi dà già un solo scrittore per campo; paga sopra ~24×24, sotto domina l'overhead delle barriere (§11) |
