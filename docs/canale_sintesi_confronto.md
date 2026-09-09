# Sintesi per il confronto con il relatore

Preparato 2026-09-08, per il confronto del pomeriggio del 2026-09-09.

**Parte I (§1–§14)** — il canale: la risposta al punto 1 della sua email e tutto il
meccanismo, per esporlo a voce.
**Parte II (§15–§24)** — il giro di correzioni manoscritte sui capitoli 3–6.

Riferimenti al codice: [`src/channel.h`](../src/channel.h), [`src/risc.c`](../src/risc.c),
[`src/grid.c`](../src/grid.c), [`asm/`](../asm/). Riferimenti alla tesi: `progetto_logico.tex`
(cap. 3), `implementazione.tex` (cap. 4), `risultati.tex` (cap. 5), `conclusioni.tex` (cap. 6).

---

# PARTE I — Il canale

---

## 1. L'obiezione e cosa colpisce davvero

Testo del relatore, punto 1:

> La cosa del canale "svuotato" è eccessiva. In realtà non interessa se il canale è pieno o
> vuoto, interessano gli stati degli indicatori. Se ISREADY, devo poterlo leggere e
> contestualmente alla lettura ISREADY deve tornare false, o (meglio), devo avere un'altra
> istruzione RESETRDY che lo rimette a false. Se la tua implementazione non usa il reset, non
> si capisce, per come è messa (forse in modo eccessivo) come funziona.

**Cosa contesta:** la presentazione. L'oggetto grammaticale della critica è *"per come è
messa"*. Dice che dal testo non riesce a ricostruire il funzionamento, non che il
funzionamento sia sbagliato. Apre con "sembra che tu abbia messo a posto diverse cosette" e
non solleva alcuna questione di correttezza in tutta l'email.

**Postura per il confronto:** non offrire riparazioni che non ha chiesto. Se la conversazione
va sul "ma funziona?", la garanzia di consegna e i 66 flip-flop da sintesi sono in §7 e §8 —
ma non tirarli fuori per primo, aprirebbero un dubbio che lui non ha posto.

---

## 2. La struttura, in termini di soli indicatori

Un canale è una struttura sola, **condivisa dalle due celle**: il canale di uscita est del
mittente *è* il canale di ingresso ovest del ricevente. Il cablaggio assegna puntatori,
non copia — [`grid.c:140`](../src/grid.c) — `b->in_ch[OVEST] = &a->out_ch[EST]`.

| campo | cos'è | chi lo scrive |
|---|---|---|
| `data_next` | registro di uscita, privato del produttore | `OUT` |
| `data` | la parola consegnata | il commit, solo sulla commutazione di `wp` |
| `wp` | indicatore del produttore (mod 2) | `SETRDY` |
| `rp` | indicatore del consumatore (mod 2) | `IN` |

Più i gemelli `wp_next`, `rp_next` (§6).

**I comparatori.** Nessun campo registra l'occupazione:

```c
ch_isrdy(c)  →  wp != rp     /* c'è una parola da leggere */
ch_iswrt(c)  →  wp == rp     /* si può consegnare */
```

Sono l'uno la negazione dell'altro. Conta la **differenza**, non il valore assoluto: dopo un
giro, "consegnato" è `wp=0, rp=1`. I contatori sono mod 2 perché la profondità è uno.

**"Pieno" e "vuoto" nel testo erano solo il nome dei due esiti del confronto.** Non esiste
una variabile di occupazione e nessuno dei due lati la scrive.

**Perché due indicatori e non un bit `full`:** un bit unico avrebbe due scrittori — il
produttore che alza, il consumatore che abbassa — nello stesso ciclo, e l'esito dipenderebbe
dall'ordine di visita. Con due indicatori ogni lato scrive solo il proprio campo. È lo stesso
argomento che regge l'assenza di lock in OpenMP (§9).

---

## 3. Le quattro istruzioni

Opcode custom-0, distinte da `funct3`:

| | `funct3` | tocca | esito |
|---|---|---|---|
| `OUT rs, dir` | 1 | `data_next` | non fallisce mai |
| `SETRDY rd, dir` | 3 | `wp_next` | 1/0 in `rd`, **l'unica rifiutabile** |
| `ISRDY rd, dir` | 2 | niente | lettura pura del comparatore |
| `IN rd, dir` | 0 | `rp_next` | ritorna `data`, e **se il confronto è vero** commuta `rp` |

- `OUT` carica il registro di uscita. Nessun indicatore si muove, nulla è ancora promesso:
  due `OUT` di fila sono due caricamenti dello stesso registro, non due messaggi.
- `SETRDY` commuta l'indicatore del produttore. **È la consegna.** Rifiutata quando il
  consumatore non ha ancora ritirato.
- `ISRDY` legge il confronto. Nessun effetto.
- `IN` restituisce la parola e commuta l'indicatore del consumatore; **su confronto falso non
  ha effetto** (§10).

Entrambi i test sono **non bloccanti**: un test bloccante in hardware richiede uno stallo di
pipeline o uno scheduler, uno non bloccante è un filo letto come un bit. Il costo in cicli lo
paga il programma, dove resta visibile e contato (`attese`, `ritentativi` in
[`risc.c:537`](../src/risc.c)).

---

## 4. Il ciclo a due fasi

Regola unica: **le letture guardano lo stato attuale, le scritture vanno nel gemello
`_next`.** [`grid.c:176-206`](../src/grid.c).

**Fase compute.** Ogni cella esegue una istruzione, tutte sullo stesso istantaneo:

| scrive | chi | istruzione |
|---|---|---|
| `data_next`, `wp_next` | il produttore | `OUT`, `SETRDY` |
| `rp_next` | il consumatore | `IN` |
| registri, PC, memoria | la cella stessa | tutte le altre |

Un solo scrittore per campo → `#pragma omp parallel for` senza alcun lock.

**Fase commit.**

```c
if (wp_next != wp) data = data_next;   /* il trasferimento */
wp = wp_next;                          /* gli indicatori passano sempre */
rp = rp_next;
```

Il ciclo visita i 4 `out_ch` di ogni cella più i canali di bordo: siccome ogni canale è
l'`out_ch` di esattamente una cella (gli `in_ch` sono puntatori) oppure appartiene alla
griglia, **ogni canale è committato una volta esatta**. Qui `running` non si guarda: i canali
di una cella terminata committano lo stesso, ed è il motivo per cui chi pubblica verso una
cella morta la trova consegnata in permanenza.

**Perché la separazione.** Senza, una cella già eseguita vedrebbe il valore *nuovo* del
vicino e una non ancora eseguita quello *vecchio*: lo stesso programma darebbe risultati
diversi al variare dell'ordine dei due cicli annidati. Con la separazione la semantica è
quella di un circuito sincrono.

La barriera implicita a fine primo loop ([`grid.c:194`](../src/grid.c)) è l'**unica**
sincronizzazione del simulatore. Il commit di bordo ([`grid.c:204`](../src/grid.c)) è
seriale: costo $O(R+C)$ del simulatore, non del modello.

---

## 5. La traccia

Produttore in $(0,0)$ manda 42 poi 43 a est. Stato iniziale `wp=0, rp=0`.

| ciclo | produttore | consumatore | stato a fine ciclo |
|---|---|---|---|
| 0 | `OUT s1, EST` (42) | `ISRDY` → 0 | `data_next=42`, `wp=0 rp=0` |
| 1 | `SETRDY` → 1 | `ISRDY` → 0 | `data=42`, `wp=1 rp=0` |
| 2 | (altro) | `ISRDY` → **1** | `data=42`, `wp=1 rp=0` |
| 3 | `OUT s1, EST` (43) | `IN t1` → **42** | `data=42`, `data_next=43`, `wp=1 rp=1` |
| 4 | `SETRDY` → 1 | … | `data=43`, `wp=0 rp=1` |

**Ciclo 1.** La `SETRDY` riesce, ma il consumatore legge ancora `wp` *attuale* = 0 e vede 0.
È il costo di attraversamento: un ciclo.

**Ciclo 3.** Il produttore carica 43 mentre il consumatore legge 42. La cattura condizionata
nel commit (`wp_next == wp`, quindi nessun trasferimento) fa sì che `data` resti 42: il 43
non ha distrutto la parola in lettura, ed è già in posizione per la `SETRDY` successiva.

**Ciclo 4.** I contatori si sono avvolti: ora "consegnato" è `wp=0, rp=1`.

Da `OUT` a `IN` passano **tre cicli** anche nel caso migliore.

---

## 6. A cosa serve ogni gemello `_next`

Vale per **ogni campo che una cella scrive e un'altra legge**.

- **`wp_next`** — `wp` lo scrive il produttore (`SETRDY`) e lo legge il consumatore
  (`ISRDY`, `IN`). Senza il gemello la consegna sarebbe istantanea o ritardata a seconda
  dell'ordine di visita.
- **`rp_next`** — `rp` lo scrive il consumatore (`IN`) ma **lo legge il produttore**:
  `ch_setrdy` valuta `ch_iswrt`, cioè `wp == rp` ([`channel.h:59`](../src/channel.h)). Il
  rilascio è informazione che attraversa il canale nel verso opposto al dato, e costa un
  ciclo anche in quel verso. Senza il gemello, con OpenMP non sarebbe nemmeno più un ordine:
  sarebbe una corsa che richiederebbe un lock.
- **`data_next`** — il valore appena caricato e quello consegnato e non ancora letto possono
  differire nello stesso istante, quindi non possono condividere gli stessi flip-flop.

In fase di compute lo stato attuale è **sola lettura per tutti** e ogni gemello è scritto da
**una cella sola**. Lettura e scrittura su due oggetti diversi: nessuna contesa da arbitrare.

---

## 7. Dove finisce il dato, e la proprietà che lo rende lecito

`IN` scrive **direttamente nel registro generale di destinazione**. Nessuna tappa intermedia,
nessun registro di ingresso lato consumatore — [`risc.c:527`](../src/risc.c):

```c
risc -> regs[d.rd] = in ? ch_read_c(in) : 0;
```

```
data_next --(commit, solo su commutazione di wp)--> data --(IN)--> regs[rd]
  produttore                                     produttore       consumatore
```

`IN` si comporta come una `LW`: sorgente il registro del vicino, destinazione un registro
generale a scelta.

**Perché è lecito senza un registro proprio.** `data` è garantita stabile per tutto
l'intervallo in cui `ISRDY` è vero:

1. `ch_commit` scrive `data` solo quando `wp_next != wp`;
2. ciò accade solo se una `SETRDY` è passata in quel ciclo;
3. `ch_setrdy` passa solo se `wp == rp`, cioè solo se il consumatore ha già ritirato.

Quindi `data` non può cambiare mentre una parola è consegnata e non letta. Il consumatore la
legge quando vuole — un ciclo dopo o mille — e trova sempre la stessa parola.

> **Questa è la vera motivazione della cattura condizionata in `ch_commit`**, migliore di
> quella che c'è oggi in tesi. Oggi la giustifico con la finestra fra `OUT` e `SETRDY`, un
> caso limite. In realtà quella riga garantisce che *l'indicatore sia vero esattamente per
> l'intervallo in cui la parola è stabile*, che è la proprietà su cui poggia tutto il lato
> consumatore. Detta così è una riga di progetto, non un caso da difendere.

---

## 8. La `SETRDY`: come commuta, quando rifiuta, la garanzia

```c
static inline int ch_setrdy(Channel *c) {
    if (ch_iswrt(c) && c -> wp_next == c -> wp) {
        c -> wp_next = c -> wp ^ 1;
        return 1;
    }
    return 0;
}
```

### Il modello mentale

**`wp_next != wp` non è uno stato, è un impulso lungo un ciclo che significa "pubblica
ora".** Lo crea la `SETRDY` in fase di compute, lo consuma il commit. **Non sopravvive mai a
un commit**, perché il commit fa `wp = wp_next`. Da cui il fatto che disorienta: a riposo i
due sono *sempre* uguali.

Su `SETRDY` **riuscita**:

| momento | `wp` | `wp_next` | |
|---|---|---|---|
| inizio compute | 0 | 0 | a riposo, uguali per costruzione |
| `SETRDY` commuta | 0 | **1** | impulso di pubblicazione alzato |
| commit | 1 | 1 | `data = data_next`, poi di nuovo a riposo |

Stato prima: `wp == rp` → consegnabile. Stato dopo: `wp != rp` → leggibile dal consumatore.

Su `SETRDY` **rifiutata**:

| momento | `wp` | `wp_next` | |
|---|---|---|---|
| inizio compute | 0 | 0 | a riposo |
| `SETRDY` rifiutata | 0 | 0 | non scrive nulla, ritorna 0 |
| commit | 0 | 0 | nessun trasferimento, stato invariato |

I due **non "tornano uguali" quando fallisce: non sono mai diventati diversi.** La differenza
la crea solo il successo. Il produttore ritenta al ciclo seguente e `data_next` lo aspetta
intatto.

Nota che commuta con `wp ^ 1`, non `wp_next ^ 1`: calcola dallo stato attuale, quindi è
idempotente — come `rp_next = rp ^ 1` in `ch_read_c`.

### I due termini della guardia rifiutano per ragioni diverse

| termine | chiede | rifiuta quando | chi lo incontra |
|---|---|---|---|
| `ch_iswrt(c)` → `wp == rp` | il consumatore ha ritirato | c'è una parola consegnata e non letta | **cella e ospite** — è la retropressione normale |
| `wp_next == wp` | nessuna `SETRDY` già passata in questo ciclo | l'impulso è già alzato | **solo l'ospite** |

Il secondo termine è vero all'inizio di *ogni* fase di compute, perché il commit precedente
lo ha ripristinato. Può essere falso solo se qualcuno ha già scritto `wp_next` **dentro il
ciclo corrente**. Una cella esegue una istruzione per ciclo e non può farlo: per una cella
quel termine è sempre aperto, e **l'unico rifiuto che può incontrare è il primo**.

L'ospite sì: `grid_push` fa `ch_write` + `ch_setrdy` in una chiamata, e due `grid_push` sullo
stesso canale di bordo nello stesso ciclo simulato alzerebbero l'impulso due volte.

**E il primo termine non lo intercetterebbe**, perché `ch_iswrt` legge `wp` e `rp`
*attuali*, che entro il ciclo non cambiano: alla seconda spinta varrebbe ancora `wp == rp` e
il termine sarebbe vero. È esattamente il motivo per cui il secondo termine esiste — senza,
la seconda spinta sostituirebbe un dato di cui la prima ha già promesso la consegna.

### La garanzia

Ogni valore pubblicato viene consegnato esattamente una volta, nell'ordine di pubblicazione.
La promessa nasce con `SETRDY`, non con `OUT`: due `OUT` senza `SETRDY` interposta sono due
caricamenti dello stesso registro, e il valore sovrascritto non era mai stato promesso a
nessuno (§12).

**Sintesi RTL** (misurati, non stimati): 66 flip-flop e 6 porte per canale, 264 FF per cella,
38 016 su $12\times12$. I 66 sono $32 + 32 + 1 + 1$; le 6 porte sono XOR e XNOR dei
comparatori più quattro abilitazioni. In Verilog **spariscono i gemelli `_next` e il commit**
— l'assegnamento non bloccante è la semantica nativa — e sparisce la guardia sulla `OUT`.
`SETRDY` pilota l'abilitazione con lo stesso filo che restituisce: è un **test-and-set**.
`ISRDY` non pilota nulla, quindi lo zero è una risposta e non un rifiuto.

---

## 9. Il resto dell'architettura, in breve

- **Array.** $R \times C$ celle, stessa immagine, quattro vicini. Nessun livello di rete,
  nessun instradamento. Il bordo alloca $2(R+C)$ canali propri, così **nessuna cella sa di
  stare sul bordo**.
- **Bordo.** L'ospite fa ciò che farebbe un vicino: `grid_push` carica e pubblica,
  `grid_pop` verifica e consuma. Nessuna scorciatoia; i rifiuti sono contati.
- **OpenMP.** Zero lock, per ragione strutturale (§4, §6). L'unica sincronizzazione è la
  barriera fra le due fasi.

---

## 10. Fatti verificati in preparazione

**La guardia su `IN` esiste.** [`channel.h:67-73`](../src/channel.h):

```c
static inline uint32_t ch_read_c(Channel *c) {
    uint32_t v = c -> data;
    if (ch_isrdy(c)) {              /* la guardia */
        c -> rp_next = c -> rp ^ 1;
    }
    return v;
}
```

È un'omissione di scrittura, non un bug. Senza la guardia, una `IN` a canale vuoto
commuterebbe `rp` e il confronto diventerebbe vero: `ISRDY` **fabbricherebbe un vero dal
nulla** e la `IN` successiva restituirebbe la vecchia `data` come fresca. Non è perdita di
fase, è un indicatore che oscilla e consegna duplicati.

**Non è il duale esatto della guardia di `SETRDY`.** `IN` rispecchia solo il primo dei due
termini; il secondo (`wp_next == wp`) non ha analogo. Non serve, per tre ragioni indipendenti:
una cella esegue una istruzione per ciclo; `grid_border_drain`
([`grid.c:285`](../src/grid.c)) visita ogni canale una volta sola; e comunque `rp ^ 1` è
**idempotente**, perché `ch_isrdy` legge lo stato attuale, che entro il ciclo non cambia.
I contatori non si sfasano in nessun caso. *Non affermare la dualità in email: è una mezza
verità che invita una domanda.*

**La temporizzazione, e il "contestualmente".** La commutazione ha effetto al commit di fine
ciclo, non dentro l'istruzione. Ma una cella esegue una istruzione per ciclo, quindi il
programma non può osservare lo stato intermedio: la prima `ISRDY` che il consumatore riesce a
eseguire vede già falso, e **allo stesso fronte** la `SETRDY` del produttore torna ad avere
esito positivo. Non è contestuale nel modello, ed è indistinguibile dal contestuale per chi
scrive assembly.

**Otto programmi su nove usano `IN`.** `memtest.s` non contiene alcuna istruzione di canale
(`macros.s` non è un programma). Rilevante per stimare il costo di `RESETRDY`.

---

## 11. `RESETRDY`, se la chiede

Renderebbe simmetriche le due sponde — `OUT`/`SETRDY` contro `IN`/`RESETRDY` — e farebbe di
`ISRDY` e `IN` due letture pure. `funct3 = 4` è libero nel custom-0. `RESETRDY` non può
fallire, quindi nessun `beqz` di ritentativo: **+1 istruzione secca per ricezione**.

| | oggi | con `RESETRDY` |
|---|---|---|
| matmul, corpo iterazione | 16 | 18 |
| matmul, passo di propagazione | 10 | 12 |
| matmul, prologo + terminazione | 19 | 19 (nessuna `IN` nel prologo) |
| matmul $12\times12$, $k{=}7$ | 335 | ~391 (+17 %) |
| jacobi, iterazione | 33 | 37 |

Due ricezioni per giro in [`asm/matmul.s`](../asm/matmul.s) (OVEST, NORD), quattro in
[`asm/jacobi.s`](../asm/jacobi.s). Proiezioni dal conteggio istruzioni, **da rimisurare**.

Legge misurata attuale: $\mathit{cicli} = 19 + 16(k-1) + 10(R+C-2)$.

**Non cambia il modello**, solo il conteggio di istruzioni: l'attraversamento resta un ciclo.
Cambia il passo di propagazione, che è costo di programma e non di modello.

---

## 12. Domande probabili

**"Perché la `OUT` non fallisce mai?"** Carica un registro tuo, che il vicino non vede.
Rifiutarla non avrebbe senso. La garanzia comincia dalla `SETRDY`.

**"Allora perché due registri dati?"** Il valore appena caricato e quello consegnato e non
ancora ritirato possono differire nello stesso istante, e non possono condividere gli stessi
flip-flop.

**"Dove finisce il dato quando fai `IN`?"** §7.

**"Perché i contatori mod 2?"** Profondità uno: bastano due stati. Un bit per lato, che per
costruzione non trabocca.

**"Se aggiungiamo `RESETRDY` cambia il modello?"** No. §11.

**In riserva, da usare solo se dice che il problema è il confine dei registri:** entrambi i
registri dati stanno dal lato produttore, e il consumatore non alloca nulla. Con un solo
slot condiviso l'unico vocabolario disponibile è l'occupazione, ed è da lì che viene
l'eccesso. Il modello con un registro per lato costa **esattamente gli stessi 66 flip-flop**
($32+32+1+1$): stesso circuito, confine disegnato altrove. La ridisegnatura è quindi gratis.
*Non aprirla per primo — è una nostra inferenza sul suo fastidio, non una cosa che ha detto.*

---

## 13. Da fare sulla tesi

1. Riscrivere il capitolo in termini di indicatori; tenere "pieno"/"vuoto" solo dove
   abbreviano, dichiarandoli come tali alla prima occorrenza.
2. Enunciare esplicitamente che `ISRDY` non ha istruzione di azzeramento e che a riportarlo a
   falso è la `IN`, con la condizione (§10) e la nota sulla temporizzazione.
3. Sostituire la motivazione della cattura condizionata con la proprietà di stabilità (§7),
   e ridurre di conseguenza la sottosezione sulla finestra fra `OUT` e `SETRDY`
   (`implementazione.tex:196`).
4. Dire chiaramente che la guardia `wp_next == wp` di `ch_write` serve **solo** a
   `grid_push` dell'ospite, e che nessuna cella può violarla.

---

## 14. Controllo aperto

`risultati.tex:796` dice 4227 cicli per Jacobi con "33 istruzioni per iterazione per 128
iterazioni, più **due** cicli per il prologo e la terminazione". Ma $33 \times 128 = 4224$ e
$4224 + 2 = 4226$. **Manca uno.** O il prologo è 3, o uno dei due fattori è diverso.
Verificare prima del confronto.

Da verificare anche sul PDF compilato: i riferimenti a tabelle 5.3 e 5.6 e alla sezione
5.5.1 citati in email.

---
---

# PARTE II — Il giro di correzioni sui capitoli 3–6

Note manoscritte sui due PDF (`concl.pdf`, cap. 6; `cap34.pdf`, capp. 3–5). **Si è fermato a
§5.5.1** — ha scritto "arrivato fino a qui" a margine della formula del prodotto di matrici.
§5.6, §5.7 e le sottosezioni finali non le ha ancora lette.

---

## 15. I tre temi ricorrenti

Più utili delle singole note: è il filo che le lega.

**1. Modello contro simulatore.** Torna almeno sei volte — `ecall` ("tutto questo della
terminazione è solo per implementare il simulatore"), `memtest` ("nella realtà dovrebbe essere
una SIGSEGV"), i due contatori di misura ("specifica che sono solo a supporto della
simulazione"), le due frecce sul codice 4.2, il cablaggio a puntatori ("nel simulatore..."),
la parallelizzazione. **Vuole che ogni affermazione dichiari a quale dei due piani
appartiene.** È la richiesta dominante di tutta la revisione.

**2. "La parallelizzazione è della simulazione di un singolo ciclo di clock."** Scritto tre
volte quasi identico: titolo del cap. 6, §6.2, §4.8. Ritiene che la formulazione attuale
sovradichiari.

**3. Lessico.** `ospite`→`host`, `immagine`→`copia`, `profondità uno`→`una posizione`,
`fili`→`collegamenti da 1 bit`, `povero`→`compatto`, e i verbi
`fissa`/`separa`/`entrano`/`ricavano`→`descrive`.

---

## 16. Errori reali che ha trovato — verificati

### 16.1 La frase sulle barriere non dice nulla

[conclusioni.tex:121-122](../TemplateTesi/conclusioni.tex): *"pagherebbe due barriere invece
di **due** per ciclo"*. Due contro due: i due membri del confronto sono lo stesso numero.
La sua graffa con "?" è giustificata — è un difetto, non oscurità. Il testo corretto è in
§19.3.

### 16.2 "Nessuna cella sa di stare sul bordo" è letteralmente falso

La sua nota (`sa` → `la posizione`) centra una contraddizione interna. Le celle conoscono
`a0`–`a3`, e [reduce.s:33](../asm/reduce.s) e seguenti fanno `beqz a1, riga_inizio` e
`beq a1, t2, fase2`: si chiede *esplicitamente* se sta sul bordo ovest e su quello est. La
tesi lo dice pure, in §5.3.6: "Qui il programma **deve** chiedersi se si trova sulla prima
colonna". **Otto programmi su nove leggono `a0`–`a3`** (solo `bordo.s` e `memtest.s` no).

La formulazione corretta è **"nessuna cella *deve* sapere"**. Che `jacobi` non usi mai la
posizione per un salto è vero e verificato: le sue nove diramazioni sono tutte ritentativi di
protocollo, e `a0`/`a1` compaiono solo nell'aritmetica del campo iniziale.

### 16.3 "E nient'altro" seguito dall'elenco che lo smentisce

[progetto_logico.tex:113](../TemplateTesi/progetto_logico.tex): *"Una cella contiene lo stato
di un processore RV32I e nient'altro:"* — e la riga dopo elenca memoria e canali. La sua nota
(`No { MEM e canali } non sono stato del processore`) è giusta: la frase si contraddice da
sola.

### 16.4 Il codice 3.1 mostra `_next` prima che esistano

*"Che non hai ancora introdotto..."* — corretto. Il listato di §3.3 espone `data_next`,
`wp_next`, `rp_next`, spiegati solo in §4.2. Il lettore vede tre campi senza semantica e un
rinvio in avanti.

### 16.5 La definizione di "cicli" è ambigua

*"I passi di clock globale **dal primo all'ultimo** processore fermo"*. La sua correzione
("dall'inizio fino al ciclo in cui si ferma") è giusta: si contano dall'avvio fino all'arresto
dell'ultima cella, non fra due processori.

### 16.6 La tabella di §4.3.2 mostra una `ISRDY` nuda

La sua nota — `while (t0==0) ISRDY t0, OVEST; IN t1, OVEST` — è corretta: **nessun kernel
reale scrive una `ISRDY` fuori da un ciclo di ritentativo**, si veda
[bordo.s:22-24](../asm/bordo.s). La tabella semplifica in un modo che non corrisponde a nessun
programma della suite.

### 16.7 L'argomento contro il bit `full` unico è girato al contrario

La nota è *"Anzi: non puoi in **hw** scrivere 2 valori nello stesso ciclo di clock"*, e la
parola chiave è **hw**.

Il testo dice che l'ordine di visita è *"un dettaglio implementativo che nel circuito reale
non esiste"*. Detta così **indebolisce il proprio argomento**: suggerisce che in hardware un
bit `full` unico andrebbe bene e che il problema sia solo del simulatore.

Il suo "Anzi:" ribalta la direzione. In hardware non è che il problema sparisce: **il progetto
non è realizzabile.** Due sorgenti che scrivono due valori sullo stesso flip-flop nello stesso
fronte sono un errore di driver multipli, e il sintetizzatore lo rifiuta. Non è
indeterminatezza, è un circuito che non esiste. È un argomento strettamente più forte, ed è
quello giusto per un capitolo di progetto — e rientra nel tema §15.1, perché la frase attuale
attribuisce al simulatore un difetto che è del progetto.

### 16.8 La freccia che non punta a niente

Sul codice 4.2 ha scritto *"questo è il codice del simulatore"* con freccia al listato, e
*"questo è il codice delle celle"* con una freccia che **non ha bersaglio**: su quella pagina
il codice della cella non c'è. Ci sono solo `ch_commit` in C e l'affermazione dei dieci cicli,
che è una proprietà del programma della cella.

Sono tre note che chiedono la stessa cosa:

- p. 18: *"Dovresti mettere un esempio di codice qui. Tipo OUT SETRDY → ISRDY IN"*
- p. 18: le due frecce, di cui una senza bersaglio
- p. 22 (§4.3.2): il `while` più la griglia ciclo-per-ciclo disegnata a mano

**Dove affermi tre cicli e dieci cicli, mostra l'assembly che la cella esegue davvero, col
ciclo di ritentativo.** Il materiale c'è già venti pagine dopo:
[bordo.s:22-27](../asm/bordo.s) è la forma canonica (Codice 5.2) e
[matmul.s:65-89](../asm/matmul.s) è il corpo da dieci cicli (Codice 5.5).

---

## 17. Ha ragione, ma il materiale esiste già altrove

Qui la risposta non è "aggiungo", è "sposto" o "richiamo".

**17.1 Il formato delle istruzioni** — c'è già, completo:
[strumenti.tex:54-63](../TemplateTesi/strumenti.tex), tabella I-type con tutti i campi bit per
bit, e §4.1 la cita. Vuole la tabella ripetuta dove introduci le quattro istruzioni.

**17.2 "Spiegare come li hai ottenuti" (i numeri di sintesi)** — Yosys è nominato a
[strumenti.tex:125](../TemplateTesi/strumenti.tex), circa quaranta pagine prima della tabella.
Serve una riga di metodo accanto ai numeri.

**17.3 "Non è solo la velocità: ci vuole anche sincronizzazione temporale d'inizio"** —
**ha ragione, e la tesi lo dimostra già** in §5.3.7: senza *ready bit* "l'ospite deve sfasare
l'ingresso, consegnando $A[i][k]$ al ciclo $i+k$". Manca solo il richiamo nelle conclusioni.
È il suo rilievo tecnicamente più sostanzioso, ed è a costo zero.

**17.4 "L'array conviene tanto più quanto è grande" → "rispetto a cosa?"** — la frase **non
ha un termine di paragone perché non ne confronta due**: mette a rapporto due grandezze
*interne* all'array, perimetro $O(R+C)$ contro griglia $O(R\cdot C)$, ma il verbo "conviene"
promette un confronto esterno che non c'è. Il termine di paragone però esiste ed è misurato in
§5.5.1: un $12\times12$ calcola 144 prodotti interni negli stessi cicli in cui un $1\times1$
ne calcola uno, a meno del riempimento. **La base è l'array $1\times1$, cioè la stessa cella
da sola.**

---

## 18. Fatti su cui ha ragione: confermare e basta

**18.1 `ecall` e RV32I.** Contestava che il testo lo facesse sembrare RV32IM. **`ECALL` è
nella base RV32I**, opcode SYSTEM `0x73` con `funct12 = 0`, insieme a `EBREAK` — come in
[risc.h:67](../src/risc.h). L'estensione M aggiunge solo `MUL`/`DIV`/`REM`, e l'unico punto
che alza a RV32IM è `matmul` per la sola `mul`. Il problema è che §4.5 introduce `ecall`
**senza dichiarare il livello di ISA**, e la dichiarazione più vicina è a venti pagine.
Basta una subordinata.

**18.2 "68 fili → collegamenti da 1 bit."** Corretto: $33 + 1 = 34$ per verso, $\times 2 = 68$
linee da un bit.

**18.3 "O ricevi da NORD o da OVEST (va bene solo per broadcast)."** Verificato:
`broadcast.s` è **l'unico** kernel con due `ISRDY` dentro lo stesso ciclo di ritentativo.
Tutti gli altri ne hanno una per ciclo, su etichette separate.

**18.4 `memtest` e la SIGSEGV.** Corretto: in hardware reale l'accesso fuori spazio è una
trap, non un `running = false` silenzioso.

**18.5 "È il grado di asincronia, il numero di posti del buffer"** (canali di profondità $N$).
Corretto, ed è una formulazione migliore di quella in tesi.

**18.6 "Solo per MM? ce ne sono altri, classici."** Corretto: convoluzione, filtri FIR,
decomposizione LU, allineamento di sequenze. Una frase basta.

**18.7 "Di solito Jacobi si fa in FP."** Corretto e importante: l'intero qui è una scelta
deliberata per verificare il pattern di comunicazione, non una semplificazione.

---

## 19. Le tre spiegazioni che ha chiesto esplicitamente

### 19.1 "Concordano sia sul rapporto a 12×12 sia sul punto di pareggio"

*("Lo capisci solo tu ...")*

Il modello analitico è uno solo: a $p$ thread lo speedup vale $W/(W/p + O)$, con $W$ il lavoro
di un ciclo e $O$ il costo delle due regioni parallele. Per usarlo serve
$W = R \cdot C \times (\text{costo di una istruzione simulata})$.

La previsione è stata costruita con **35 ns per istruzione**, misurati su $64\times64$. Quel
valore è sbagliato se applicato a griglie piccole, perché il costo per istruzione **non è
costante**: varia da 15,9 a 83,4 ns con la dimensione, dato che il passo di 16 KB fra celle
contigue disperde lo stato caldo.

Il modello ha quindi prodotto **due** previsioni, entrambe sbagliate, entrambe nella stessa
direzione, entrambe per lo stesso identico motivo:

| | previsione (35 ns) | misura | rifatta col costo giusto |
|---|---|---|---|
| rapporto a $12\times12$ | $0{,}28\times$ | $0{,}175\times$ | $\mathbf{0{,}179\times}$ (22,5 ns) |
| punto di pareggio | $24\times24$ | $40\times40$ | **appena sotto** $\mathbf{40\times40}$ (16–23 ns) |

Entrambe le rifatture cadono sulla misura. **Questo è il "concordano": non è il modello a
sbagliare, è il numero che gli si dava in ingresso.** I due conti sono a
[risultati.tex:1075-1080](../TemplateTesi/risultati.tex) e
[risultati.tex:1203-1210](../TemplateTesi/risultati.tex).

La frase attuale enuncia il risultato senza nessuno dei due numeri, senza dire fra che cosa
cadano i due accordi, e soprattutto **senza dire che è una sola causa a spiegare entrambi gli
scarti**.

> **Sostituzione.** Il modello analitico prevede lo speedup come $W/(W/p + O)$, e le due
> previsioni che ne erano state tratte cadevano entrambe lontano dalla misura: $0{,}28\times$
> contro $0{,}175\times$ per il rapporto su $12\times12$, e $24\times24$ contro $40\times40$
> per il punto di pareggio. Lo scarto ha una causa sola, e non è il modello: entrambe
> assumevano 35 ns per istruzione simulata, un valore misurato su $64\times64$ dove la
> località è peggiore. Rifatte con il costo misurato sulla forma che si sta predicendo —
> 22,5 ns su $12\times12$, fra 16 e 23 nell'intervallo del pareggio — danno $0{,}179\times$ e
> una collocazione appena sotto $40\times40$, cioè le due misure. Il modello è corretto; a non
> esserlo era il costo per istruzione che gli si dava in ingresso.

### 19.2 Che cosa "si semplifica"

Siano $N$ i cicli eseguiti, $W$ il lavoro di un ciclo, $O$ il costo delle due regioni per
ciclo, $p$ i thread:

$$T_1 = N \cdot W \qquad\qquad T_p = N\left(\frac{W}{p} + O\right) \qquad\qquad
\frac{T_1}{T_p} = \frac{N \cdot W}{N\left(\frac{W}{p} + O\right)} = \frac{W}{\frac{W}{p} + O}$$

**$N$ compare identico sopra e sotto e si cancella.** È tutto qui: una cancellazione
algebrica.

*Perché serve "una istruzione per cella":* è l'ipotesi che rende $W$ **lo stesso in ogni
ciclo**. `grid_step` esegue sempre esattamente $R \cdot C$ istruzioni simulate e aggiorna
$4R \cdot C$ canali, qualunque cosa le celle eseguano. Con lavoro variabile avresti $W(t)$, e
$\sum_t W(t) \,/\, \sum_t (W(t)/p + O)$ non si semplifica affatto.

*Conseguenza:* lo speedup non dipende da $N$, quindi non da **che cosa** il programma calcola,
ma solo da $R \cdot C$. Un programma a maggiore intensità di calcolo non rende i cicli più
pesanti, ne esegue di più. Il controllo sperimentale: `PESO=1, ITER=62` contro
`PESO=32, ITER=8` eseguono entrambi 2624 cicli differendo di trentadue volte nel calcolo per
comunicazione, e su $32\times32$ danno $0{,}78\times$ contro $0{,}77\times$.

> **Sostituzione.** Un ciclo è per costruzione una istruzione per cella, quindi il lavoro $W$
> di un ciclo è lo stesso in tutti i cicli e vale $R \cdot C$ istruzioni simulate. Il tempo
> seriale è allora $N \cdot W$ e quello parallelo $N(W/p + O)$, dove $N$ è il numero di cicli
> eseguiti: nel rapporto fra i due, $N$ compare a numeratore e a denominatore e si cancella,
> lasciando $W/(W/p + O)$. Lo speedup non dipende dunque da quanti cicli il programma esegue,
> cioè da che cosa calcola, ma solo da $R \cdot C$.

### 19.3 Barriere contro aperture di regione, e la risposta a "(thread pool?)"

**Oggi:** [grid.c:187-202](../src/grid.c) — le due `#pragma omp parallel for` stanno *dentro*
`grid_step`, chiamata una volta per passo. Per ogni ciclo paghi **due aperture di regione
parallela**: formazione della squadra, distribuzione delle iterazioni, barriera implicita,
chiusura. Costo misurato: 3,87 µs a due thread e 13,89 µs a sedici, due volte per passo.

**Spostandola:**

```c
#pragma omp parallel              /* UNA regione per l'intera esecuzione */
for (ogni passo) {
    #pragma omp for               /* barriera */
        ...compute...
    #pragma omp for               /* barriera */
        ...commit...
}
```

Una sola apertura per tutta la simulazione, e per ciclo restano **due barriere**: la squadra
esiste già e deve solo ritrovarsi.

**Perché abbassa il pareggio:** il pareggio è dove lo speedup vale 1, cioè dove
$W/p + O = W$, cioè $O = W\frac{p-1}{p}$. A otto thread $W = \frac{8}{7}O$, ed è esattamente
l'$8/7$ della didascalia di [risultati.tex:1040](../TemplateTesi/risultati.tex). Un $O$ più
piccolo richiede un $W$ più piccolo per pareggiare, quindi un $R \cdot C$ più piccolo.

**La risposta al suo "(thread pool?)":** sì, OpenMP tiene già un pool, quindi aprire una
regione non crea thread. Ciò che lo spostamento elimina non è la creazione dei thread — quella
è già una volta sola, ed è la stessa che a
[risultati.tex:1229-1231](../TemplateTesi/risultati.tex) attribuisci all'ordinamento delle
curve per `PESO` — ma **la formazione della squadra e il fork-join a ogni ciclo**, che è ciò
che il micro-benchmark da 3,87–13,89 µs misura.

> **Sostituzione.** Spostare la regione parallela fuori dal ciclo dei passi lascerebbe due
> barriere per ciclo al posto di due aperture di regione parallela per ciclo: la squadra di
> thread verrebbe formata una volta sola per l'intera esecuzione e a ogni ciclo resterebbe
> soltanto il costo di due rendez-vous. Poiché il pareggio cade dove $O$ eguaglia
> $W(p-1)/p$, ridurre $O$ riduce il lavoro per ciclo necessario a pareggiare, e quindi
> $R \cdot C$: è il modo di abbassare il pareggio senza ingrandire la griglia.

---

## 20. "Il pareggio è misurato a 40×40": cosa significa

Dal metodo in [risultati.tex:1150-1155](../TemplateTesi/risultati.tex): sweep di dodici forme
da $8\times8$ a $256\times256$, cinque conte di thread, tre valori di `PESO`, nove ripetizioni
per punto di cui si prende il minimo. La forma $40\times40$ è stata **aggiunta apposta** per
infittire lo sweep dove la curva attraversa il pareggio: senza, la collocazione resterebbe
indeterminata fra $32\times32$ e $48\times48$.

Significa quindi: **su una griglia di $40\times40$, cioè 1600 celle RISC-V simulate,
parallelizzare il ciclo di clock comincia a rendere.** Sotto quella forma i thread fanno
perdere tempo, sopra lo fanno guadagnare.

Nelle conclusioni "$40\times40$" è un numero nudo: il lettore non sa che è una forma di
griglia e non una conta di thread, non sa che sono 1600 celle, non sa fra che cosa cada il
pareggio. Da lì vengono anche il "non è univocamente determinato" e la nota **"gradi di
parallelismo"**, che è la distinzione che vuole tenuta: $40\times40$ è la *dimensione del
problema*, la conta di thread è il *grado di parallelismo*, e §6.2 li mescola — il capoverso
porta anche il $24\times24$ della previsione, quindi due numeri con due significati e nessuno
dei due spiegato.

---

## 21. §4.7.1, il disegno del clock

Ha cerchiato *"sul fronte"* e disegnato la forma d'onda: la scrittura a un fronte, il valore
leggibile da lì e stabile fino al fronte successivo.

La sostanza: la frase *"un ingresso campionato sul fronte non consente per costruzione due
caricamenti nello stesso ciclo"* è **corretta ma asserita, non argomentata**. Non è il fronte
a impedire il secondo caricamento — è che fra due fronti l'ingresso porta un solo valore,
quindi c'è un campionamento per periodo. Il suo disegno *è* l'argomento mancante.

Sulla stessa pagina ha scritto **"? quale"** su *"la guardia sulla `OUT` discussa nella
sezione 4.3"*: non è riuscito a identificarla. La frase è quindi illeggibile due volte — una
guardia che non si sa quale sia sparisce per una ragione che non viene mostrata.

> ⚠️ **Da controllare sul foglio.** Prima del segno d'onda c'è un tratto che sembra un "No".
> Se lo è, sta contestando *"sul fronte"* come spiegazione; se è altro, sta solo illustrando.
> Cambia se è una correzione o un'aggiunta.

---

## 22. Correzioni di forma e lessico

Da applicare senza discussione, una volta deciso il punto §23:

- `ospite` → `host` (oppure `programma ospite`: **vedi §23**)
- `immagine` → `copia` ("tutte le celle eseguono la stessa copia")
- `profondità uno` → `una posizione` (regola 3 del modello, e §3.3)
- `fili` → `collegamenti da 1 bit`
- `deliberatamente povero` → `molto compatto`
- verbi di struttura: `fissa`, `separa`, `entrano`, `ricavano` → `descrive`
- `configurando un test-and-set` → `comportandosi di fatto come un test-and-set`
- `in cicli è nullo` → `in termini di cicli impiegati il costo è nullo`
- `in area` → `in termini di occupazione`
- §5.1: `le misure` → `i risultati degli esperimenti effettuati`
- §5.3.9: aggiungere che `pesante` **è un benchmark sintetico**
- §5.3.4: `Si autoverifica e lascia il verdetto` → `Il programma lascia l'esito della verifica`
- Tabella 5.1, didascalia: togliere "e i sette di verifica"
- Tabella 5.3: la colonna `somma` va spiegata (è il risultato accumulato dal consumatore)
- §4.6 e ovunque: `l'ospite si affaccia` → `l'host interagisce con`
- `le due regioni parallele che il ciclo apre` → nominarle (compute e commit)
- §3.4: dire esplicitamente che **la comunicazione di bordo segue lo stesso protocollo di
  quella interna** — è il punto della sezione e non è scritto
- didascalia fig. 3.1: menzionare che la posizione è ciò che differenzia il comportamento
- §3.2: dei due contatori di misura, dire che sono **solo a supporto della simulazione**
- §4.8: i due capoversi vanno resi elenco puntato (ha messo due pallini a margine)
- §5.1: `sweep` va introdotto molto prima, alla prima occorrenza

**Diagrammi richiesti** ("Farebbe comodo un disegno per ognuno dei codici"): esistono le
figure per `broadcast`, `chain`, `reduce`, `matmul`. Mancano `prodcons`, `bordo`, `memtest`,
`jacobi`, `pesante` — e per i primi due ha disegnato lui lo schizzo a margine.

---

## 23. Domande da porgli

**`ospite` o `host`?** Sulla prima pagina del cap. 3 ha scritto "io direi *programma ospite*
invece che *ospite*". Poi, da p. 13 in avanti, ha corretto `ospite` → `host` almeno sei volte.
Sono due direzioni opposte e la sostituzione è globale: **fattela decidere prima di
toccarla.**

**Il "No" di §21**, se il segno sul foglio è quello.

---

## 24. Le tre volte che ha scritto "non capisco"

Vale la pena guardarle insieme: indicano dove la scrittura cede, e nessuna è un errore di
contenuto.

- §5.1, il campo iniziale dello stencil: *"questo caso non ci capisco proprio!"* e *"che vuole
  dire?"* su "contorno costante 64";
- §5.5.1, la formula: si ferma lì (*"arrivato fino a qui"*);
- §6.2: *"Lo capisci solo tu ..."* — trattata in §19.1.

Sono tre frasi che comprimono troppo. La terza in particolare è una conclusione che rinvia a
due misure senza ripeterne nessuna.

---

## 25. Stato e controlli aperti

- **§14 resta aperto**: l'off-by-one su Jacobi ($33 \times 128 + 2 = 4226$ contro 4227
  riportati). Non ci è ancora arrivato — si è fermato a §5.5.1 — quindi c'è tempo, ma è
  esattamente il tipo di errore che ha già preso una volta.
- Verificare sul PDF compilato i riferimenti a tabelle 5.3 e 5.6 e a §5.5.1 citati in email.
- Il "No" di §21.
- Restano da leggere da parte sua: §5.6, §5.7 e le sottosezioni finali del cap. 5.
