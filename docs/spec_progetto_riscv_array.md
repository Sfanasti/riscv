# Simulatore di array RISC-V con sincronizzazione NESO — Specifica di progetto

> Documento di specifica per estendere l'interprete **stella** (ISS RISC-V scritto in C).
> Obiettivo: simulare una griglia di processori RISC-V che comunicano coi vicini
> cardinali (Nord/Est/Sud/Ovest) tramite registri di bordo + handshake produttore-consumatore.

---

## 1. Contesto e obiettivo

- Si parte da **stella**, un interprete C che esegue il ciclo fetch–decode–execute di **un singolo** core RISC-V.
- Va esteso su due livelli:
  1. **Singolo core**: aggiungere due nuove istruzioni `ISRDY` e `SETRDY`.
  2. **Array**: un *interprete main* che istanzia una topologia di core (es. matrice 5×3 o stringa lineare di 10), li fa avanzare di N cicli ciascuno, e gestisce i registri di bordo.
- È tutto **software** (nessun hardware/FPGA). Il modello hardware (mesh/NoC) resta solo come riferimento concettuale.
- La comunicazione è **registro-a-registro tra vicini**, non una NoC con routing: niente pacchetti, niente instradamento, solo scambio diretto con segnalazione di "pronto".

---

## 2. Modello architetturale

### Registri per processore
Ogni core ha:
- i **32 registri standard** `x0..x31` (con `x0` hardwired a 0);
- **4 registri OUT**, uno per direzione: `OUT[N]`, `OUT[E]`, `OUT[S]`, `OUT[W]`;
- **4 registri IN**, uno per direzione: `IN[N]`, `IN[E]`, `IN[S]`, `IN[W]`.

Quindi **8 registri di bordo** in più rispetto ai 32 standard.

### Canale tra vicini
Il punto chiave: il registro di uscita di un nodo verso una direzione **è** il registro di ingresso del vicino dalla direzione opposta. Sono i due lati dello stesso **canale**:

```
   (i,j) OUT[E]  ───────────►  IN[W] (i,j+1)
```

Un canale ha: uno **slot dati** (il valore) e **un bit di ready** condiviso (profondità 1).

### Mappatura direzioni (FISSA, da usare ovunque)
```
N = 0,  E = 1,  S = 2,  W = 3
```
Direzione opposta:
```
opp(d) = d ^ 2      // N<->S (0^2=2), E<->W (1^2=3)
```
Regola di accoppiamento: il produttore usa `SETRDY[d]`, il consumatore usa `ISRDY[opp(d)]` sullo **stesso** canale.

---

## 3. Le due nuove istruzioni

### Semantica (entrambe test NON bloccanti)
- **`ISRDY rd, dir`** — *gate del consumatore*. Mette in `rd` il valore `1` se c'è un dato leggibile da `IN[dir]`, altrimenti `0`. Non blocca.
- **`SETRDY rd, dir`** — *gate del produttore*. Pubblica il dato già scritto in `OUT[dir]`: se lo slot era libero (il dato precedente è stato consumato) marca il canale come pieno e mette `1` in `rd`; se era ancora pieno (non ancora letto dal vicino) **non** sovrascrive e mette `0` in `rd`. Non blocca.

`SETRDY` restituisce un esito apposta per essere **simmetrica** a `ISRDY`: così il produttore-consumatore è completo (vedi §4). I loop d'attesa si scrivono in assembly.

### Encoding (proposta — da rispecchiare nel decoder di stella)
- Opcode **custom-0 = `0b0001011` (0x0B)** — spazio riservato RISC-V, garantito libero.
- Formato **I-type**:
  ```
  | imm[11:0] | rs1 | funct3 | rd | opcode |
  |  31..20   |19.15| 14..12 |11.7|  6..0  |
  ```
- Campi usati:
  - `opcode` = `0x0B`
  - `funct3` = `000` per `ISRDY`, `001` per `SETRDY`
  - `imm[1:0]` = direzione (`00`=N, `01`=E, `10`=S, `11`=W); resto dell'immediato = 0
  - `rs1` = `x0` (non usato)
  - `rd` = registro destinazione dell'esito (0/1)

> NB: l'encoding è una proposta. Poiché il decoder lo scriviamo noi in stella, l'unico
> vincolo è che `.s` e decoder usino **lo stesso** schema.

### Come emetterle nei file `.s`
Senza patchare l'assembler, due strade in GNU `as`:
- direttiva **`.insn`** (NB: `.insn`, non `.inst` che è ARM):
  ```asm
  .macro isrdy rd, dir
      .insn i 0x0B, 0, \rd, x0, \dir
  .endm
  .macro setrdy rd, dir
      .insn i 0x0B, 1, \rd, x0, \dir
  .endm
  ```
- oppure **`.word 0x........`** con l'encoding precalcolato (funziona sempre).

---

## 4. Protocollo di sincronizzazione (cuore del progetto)

Handshake **produttore-consumatore completo, profondità 1, a 1 bit**, realizzato con due
contatori mod 2 (`wp`, `rp`) + comparatore.

### Stato del canale
```
pieno / leggibile   :  wp != rp
vuoto / scrivibile  :  wp == rp
```

### Le tre operazioni
- **Scrittura dato** in `OUT[d]` (meccanismo dati, vedi §8 punto A): scrive solo il valore, non tocca i bit.
- **`SETRDY d`** (produttore): se `wp == rp` (vuoto) → `wp ^= 1` (ora pieno), ritorna 1. Altrimenti ritorna 0 (slot ancora occupato, non sovrascrivere).
- **`ISRDY opp(d)`** (consumatore): ritorna `(wp != rp) ? 1 : 0`.
- **Lettura dato** da `IN[opp(d)]` (consume-on-read): ritorna il valore e, se il canale era pieno, `rp ^= 1` (libera lo slot).

Questo chiude il "cerchio": il produttore non riscrive finché il consumatore non ha letto, e il
consumatore non legge finché il produttore non ha pubblicato. Niente dato perso (no race condition).

### Loop d'attesa in assembly (i test sono non bloccanti)
Produttore — manda verso Est:
```asm
    # ... scrivi il dato in OUT[E] (csrw o equivalente, vedi §8.A) ...
spin_p:
    setrdy a0, 1          # dir E = 1 ; a0 = 1 se pubblicato, 0 se slot ancora pieno
    beqz   a0, spin_p     # riprova finché il vicino non ha consumato
```
Consumatore — riceve da Ovest:
```asm
spin_c:
    isrdy a0, 3           # dir W = 3 ; a0 = 1 se c'è un dato
    beqz  a0, spin_c
    # ... leggi IN[W] (csrr o equivalente) -> la lettura consuma e libera lo slot ...
```

### Struttura dati C (proposta)
```c
typedef struct {        // canale direzionato, profondità 1
    uint32_t data;      // OUT[d] del mittente == IN[opp(d)] del ricevente
    uint8_t  wp;        // write pointer (mod 2)
    uint8_t  rp;        // read  pointer (mod 2)
} Channel;

static inline int  ch_readable(const Channel *c){ return c->wp != c->rp; }
static inline int  ch_writable(const Channel *c){ return c->wp == c->rp; }
// setrdy: se writable, wp ^= 1 e ritorna 1; altrimenti 0
// isrdy : ritorna ch_readable()
// read  : valore = data; se readable, rp ^= 1 (consume)
```

---

## 5. L'interprete main (driver dell'array)

### Input
- Una **topologia**: stringa lineare (es. 10 nodi) oppure matrice (es. 5×3).
- Il **programma** che ogni nodo esegue (uguale per tutti o per-nodo — da decidere, vedi §8).
- Numero di **cicli** da simulare.

### Strutture
- Un array/matrice di stati core (PC, register file, regs di bordo).
- I **canali** tra coppie di vicini (uno per edge; profondità 1).
- Mappatura griglia → canali: per ogni nodo `(i,j)` e direzione `d`, il canale verso il vicino.

### Ciclo di esecuzione — TWO-PHASE UPDATE (importante)
Per non dipendere dall'ordine di scansione e simulare un array sincrono in modo riproducibile:
1. **Fase 1 (compute)**: tutti i core eseguono un passo fetch-decode-execute leggendo lo
   stato dei canali **committato a inizio ciclo**. Le modifiche a `wp`/`rp`/`data` vanno in
   un buffer "next", non subito sullo stato visibile.
2. **Fase 2 (commit)**: si applicano in blocco tutti gli aggiornamenti dei canali.

Conseguenza voluta: la comunicazione ha **latenza 1 ciclo per hop** (il vicino vede il dato il
ciclo successivo), esattamente come in un array systolic reale. Questo riguarda la *fedeltà
temporale*; la *correttezza* del dato è già garantita dall'handshake.

### Registri di bordo (confine della griglia)
- I nodi sul bordo hanno direzioni "verso l'esterno": le gestisce l'**host** (l'interprete main),
  che tra un ciclo e l'altro **scrive** gli IN di bordo (input dall'array) e **legge** gli OUT
  di bordo (output dall'array).
- **Bordi aperti** (no wrap-around) salvo diversa indicazione del prof — vedi §8.

---

## 6. Caso d'uso dimostrativo: moltiplicazione di matrici systolic

Buon test/demo per la valutazione: un **array systolic** per `C = A × B`, dove i dati scorrono
verso S+E, ogni cella fa multiply-accumulate e passa i valori al vicino. È l'applicazione tipica
di questa architettura (tipo TPU). Qui usiamo il meccanismo produttore-consumatore (self-timed con
ready bit) invece della tempistica systolic in lockstep: scelta più generale e robusta.

---

## 7. Scomposizione dei task

1. **Aggiungere le istruzioni `ISRDY [d]` e `SETRDY [d]`** (controllo/segnalazione del dato per direzione).
   - **1a.** Definire l'encoding RISC-V (opcode custom-0 `0x0B`, formato I-type, §3) e le macro
     `.insn`/`.word` per usarle nei `.s`.
   - **1b.** Estendere il decode/execute di stella per interpretare le due nuove istruzioni,
     con un *hook* verso il livello array (un singolo core non comunica da solo).
2. **Implementare la sincronizzazione** (coppia di bit `wp`/`rp` mod 2 + comparatore, profondità 1, §4).
   - **2a.** Implementare `Channel` e le operazioni `setrdy`/`isrdy`/`read-consume`.
   - **2b.** Implementare l'interprete main: prende la topologia, fa avanzare tutti i core di N
     cicli con **two-phase update**, e alimenta/legge i registri di bordo.

---

## 8. Decisioni aperte (da confermare col prof o all'arrivo di stella)

- **A. Percorso dati OUT/IN.** Le due nuove istruzioni gestiscono solo il *bit di ready*. Come si
  **scrive** `OUT[d]` e si **legge** `IN[d]`? Proposta: via **CSR** a indirizzi custom
  (`csrw out_e, a0` / `csrr a0, in_w`), così restano solo 2 nuove istruzioni. Alternative: indici
  GPR estesi, oppure due ulteriori istruzioni custom `PUT/GET`. **Da fissare in base a come stella
  espone i registri.**
- **B. Programma per nodo.** Tutti i core eseguono lo stesso `.s` o programmi diversi per nodo?
- **C. Bordi.** Aperti + I/O host (proposta) oppure wrap-around (toro)?
- **D. Semantica del confronto col systolic del prof.** Produttore-consumatore stretto con ready
  bit (proposta) vs tempistica systolic in lockstep senza backpressure. Confermare l'uso voluto.
- **E. Formato di input di stella.** Carica un binario/hex (allora pipeline: `as` → `objcopy` →
  stella) o un altro formato? Da verificare sul codice.

---

## 9. Setup dell'ambiente di sviluppo (VERIFICATO E TESTATO)

> Tutti i comandi qui sotto sono stati provati con `binutils-riscv64-unknown-elf` 2.42.
> Lavori su due livelli — **C** (modifichi stella) e **assembly RISC-V** (i `.s`) — quindi
> servono un toolchain C e un cross-toolchain RISC-V.

### 9.1 Sistema operativo
- **Linux nativo** (Ubuntu/Debian): la via più liscia, consigliata.
- **Windows → WSL2** con Ubuntu. NON Windows nativo: la toolchain RISC-V lì è un dolore.
- **macOS**: funziona ma i pacchetti hanno altri nomi (via Homebrew).

### 9.2 Cosa installare (Ubuntu / WSL2) — un solo comando
```bash
sudo apt update
sudo apt install -y build-essential gdb make git binutils-riscv64-unknown-elf
# OPZIONALE (compilatore C cross + newlib, solo se vorrai linkare C o startup):
sudo apt install -y gcc-riscv64-unknown-elf
```
A cosa serve ciascun pacchetto:
- `build-essential`, `gdb`, `make` → **compilare e debuggare stella** (è in C).
- `git` → **versionare il progetto** (fallo da subito: stella, i `.s`, questo spec).
- `binutils-riscv64-unknown-elf` → l'**assembler RISC-V** e gli strumenti (`as`, `ld`, `objcopy`,
  `objdump`, `readelf`). È il **minimo indispensabile** per assemblare i `.s`.
- `gcc-riscv64-unknown-elf` (opzionale) → solo se serve il compilatore C cross o il linking con startup.

### 9.3 Due trappole già verificate (IMPORTANTI)
1. **Il pacchetto si chiama `riscv64-unknown-elf`, NON `riscv32`.** È lo stesso toolchain: genera
   **rv32** con i flag giusti (sotto). Non cercare un pacchetto `riscv32-`: non esiste.
2. **Le istruzioni CSR richiedono l'estensione Zicsr**: usa `-march=rv32i_zicsr`. Senza, l'assembler
   dà `Error: ... extension zicsr required`.

### 9.4 Comandi di build (testati)
```bash
# 1) assembla un .s per RV32I (+ Zicsr per le istruzioni CSR)
riscv64-unknown-elf-as -march=rv32i_zicsr -mabi=ilp32 prog.s -o prog.o

# 2) ispeziona l'encoding (qui verifichi le custom ISRDY/SETRDY)
riscv64-unknown-elf-objdump -d prog.o

# 3) estrai il binario grezzo da dare in pasto a stella
riscv64-unknown-elf-objcopy -O binary prog.o prog.bin
```
Flag fissi: **`-march=rv32i_zicsr -mabi=ilp32`** (rv32, interi a 32 bit). Aggiungi estensioni solo se
servono (es. `rv32im_zicsr` per moltiplicazione/divisione).

### 9.5 Editor e workflow
- **VS Code** + estensioni: C/C++, una per la **sintassi RISC-V assembly**, Makefile Tools.
- **Claude Code** dentro VS Code → per lavorare nel repo con assistenza inline.
- **git** dall'inizio: commit di stella, dei `.s` e di questo documento.

### 9.6 Opzionale (utile più avanti)
- **Spike** (ISS ufficiale RISC-V): riferimento per validare il comportamento delle istruzioni *standard*.
- **qemu-user** (`qemu-riscv32`): per eseguire al volo programmi rv32 standard fuori da stella.

### 9.7 Pipeline di build/run
```
prog.s --(as, con .insn)--> prog.o --(objcopy -O binary)--> prog.bin --> stella --> run N cicli
```
Le istruzioni custom passano per `.insn`/`.word`; stella le decodifica con lo schema di §3.
(Il formato di input di stella è da confermare — decisione E.)

---

## Punti su cui NON transigere (per evitare bug silenziosi)
1. **Handshake completo**: `SETRDY` deve restituire l'esito e il produttore deve controllarlo
   prima di riscrivere; la lettura deve consumare. Senza → dato perso.
2. **Two-phase update** nel main: leggere lo stato committato, applicare i toggle a fine ciclo.
   Senza → risultato dipendente dall'ordine di scansione, non riproducibile.
3. **Una sola mappatura direzioni** (`N=0, E=1, S=2, W=3`, `opp = d^2`) usata ovunque.
