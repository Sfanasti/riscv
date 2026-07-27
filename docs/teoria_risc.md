# Teoria del progetto — Array di core RISC-V con sincronizzazione NESO

> Documento di teoria: **cosa** stiamo costruendo, **come** e soprattutto **perché**.
> Compagno operativo della scaletta in [ROADMAP.txt](ROADMAP.txt) e della specifica
> [spec_progetto_riscv_array.md](spec_progetto_riscv_array.md).

---

## 1. Cosa stiamo implementando (in una frase)

Estendiamo **stella** — un interprete (ISS, *Instruction Set Simulator*) RISC-V
scritto in C — perché, oltre a eseguire un singolo core, sappia simulare una
**griglia di core** che comunicano coi vicini cardinali (Nord/Est/Sud/Ovest)
scambiandosi dati con un **handshake produttore-consumatore** sicuro.

In concreto aggiungiamo **due nuove istruzioni** alla ISA, `ISRDY` e `SETRDY`,
che gestiscono la *segnalazione di "pronto"* tra core vicini.

---

## 2. Il punto di partenza: stella e RV32

Stella è già un interprete funzionante. Esegue il classico ciclo:

```
fetch  ->  decode  ->  execute     (in core.c)
```

- **fetch**: legge la prossima istruzione dalla memoria all'indirizzo `pc`.
- **decode**: spacchetta i 32 bit dell'istruzione nei campi (`opcode`, `rd`,
  `funct3`, `rs1`, `rs2`, `funct7`, immediato).
- **execute**: uno `switch` sull'`opcode` applica l'effetto al register file.

Stella è a **32 bit (RV32I + estensione M** per moltiplicazione/divisione**)**:
registri e parole sono `uint32_t`, e il loader ELF accetta solo binari a 32 bit.
Il toolchain si chiama `riscv64-unknown-elf` ma è solo il nome del pacchetto:
con i flag `-march=rv32i_zicsr -mabi=ilp32` genera codice **rv32**.

> **Perché un ISS e non hardware?** Il progetto è tutto software. Il modello
> hardware (mesh/NoC) resta solo riferimento concettuale: niente FPGA, niente
> pacchetti, niente routing. La comunicazione è **registro-a-registro tra
> vicini diretti**.

---

## 3. Il modello architetturale: l'array

### Registri di ogni core
Oltre ai 32 registri standard `x0..x31` (con `x0` fisso a 0), ogni core ha
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

Sono **test non bloccanti** (non fermano mai il core):

- **`ISRDY rd, dir`** — *gate del consumatore*. Mette in `rd` il valore `1` se in
  `IN[dir]` c'è un dato leggibile, altrimenti `0`.
- **`SETRDY rd, dir`** — *gate del produttore*. Pubblica il dato già scritto in
  `OUT[dir]`: se lo slot era libero (il dato precedente è stato consumato) marca
  il canale come pieno e mette `1` in `rd`; se era ancora pieno **non**
  sovrascrive e mette `0`.

> **Perché non bloccanti?** È la scelta di design più importante. Se le
> istruzioni bloccassero in attesa, la logica di sincronizzazione sarebbe
> nascosta dentro l'interprete C. Rendendole semplici test che ritornano un
> esito, **i loop di attesa si scrivono in assembly RISC-V** — ed è lì che vive
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

## 5. Come si codifica una istruzione "nuova" in RISC-V (task 1.a)

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
Assemblato [test_isa.s](test_isa.s) con
`riscv64-unknown-elf-as -march=rv32i_zicsr -mabi=ilp32` e disassemblato con
`objdump -d`. L'encoding combacia con quello calcolato a mano:

| Istruzione | Encoding |
|-----------|---------|
| `OUT a0, E`    | `0x0015100b` |
| `SETRDY a1, E` | `0x0010358b` |
| `ISRDY a2, W`  | `0x0030260b` |
| `IN a3, W`     | `0x0030068b` |

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

Struttura dati C (la implementeremo in `channel.h`):
```c
typedef struct {        // canale direzionato, profondita' 1
    uint32_t data;      // OUT[d] del mittente == IN[opp(d)] del ricevente
    uint8_t  wp, rp;    // write / read pointer (mod 2)
} Channel;
```

---

## 7. Dove vive davvero la logica: l'assembly

Poiché le istruzioni sono non bloccanti, i **loop di attesa** sono codice
RISC-V che scriviamo noi:

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

Il driver dell'array fa avanzare tutti i core di N cicli. Per **non dipendere
dall'ordine** in cui scandisce i core, ogni ciclo è diviso in due fasi:

1. **Fase compute**: tutti i core eseguono un passo leggendo lo stato dei canali
   **committato a inizio ciclo**; le modifiche a `wp`/`rp`/`data` vanno in un
   buffer "next", non subito sullo stato visibile.
2. **Fase commit**: si applicano in blocco tutti gli aggiornamenti.

> **Perché two-phase?** Senza, il risultato dipenderebbe dall'ordine di scansione
> (il core 0 vedrebbe già gli effetti del proprio vicino nello stesso ciclo) e la
> simulazione non sarebbe **riproducibile**. Conseguenza voluta: la comunicazione
> ha **latenza 1 ciclo per hop**, esattamente come un array systolic reale. È
> fedeltà temporale: la *correttezza* del dato è già garantita dall'handshake.

> **Nota sulla stella della tesi:** il suo `run_multi_core` usa uno "snapshot"
> che in pratica è codice morto (copia `in_reg` su se stesso) e non implementa il
> two-phase vero. Lo riscriveremo nella Fase 3 del ROADMAP.

### Registri di bordo
I nodi sul bordo della griglia hanno direzioni "verso l'esterno": le gestisce
l'**host** (l'interprete main), che tra un ciclo e l'altro scrive gli `IN` di
bordo (input nell'array) e legge gli `OUT` di bordo (output dall'array). Bordi
**aperti** (no wrap-around) salvo diversa indicazione.

---

## 9. Il caso d'uso dimostrativo: moltiplicazione di matrici systolic

La demo finale è un **array systolic** per `C = A × B`: i dati scorrono verso
S+E, ogni cella fa un *multiply-accumulate* e passa i valori al vicino. È
l'applicazione tipica di questa architettura (tipo TPU). Usiamo il meccanismo
produttore-consumatore (self-timed, con ready bit) invece della tempistica
systolic in lockstep: scelta più generale e robusta.

---

## 10. Riepilogo: cosa, come, perché

| Cosa | Come | Perché |
|------|------|--------|
| 2 istruzioni `ISRDY`/`SETRDY` | opcode `0x0B`, I-type, funct3 2/3, via `.insn` | segnalare "pronto" senza patchare l'assembler |
| handshake produttore-consumatore | `Channel` con `wp`/`rp` mod 2, profondità 1 | nessun dato perso, nessuna race |
| istruzioni non bloccanti | ritornano un esito; spin-loop in assembly | la logica vive nell'assembly, l'interprete resta minimale |
| two-phase update nel driver | leggi stato committato, applica i toggle a fine ciclo | riproducibilità + latenza 1 ciclo/hop come un array systolic |
| una sola mappatura direzioni | `N=0 E=1 S=2 W=3`, `opp=d^2` | elimina i bug silenziosi di accoppiamento |
