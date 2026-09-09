# Risposte del relatore al documento di sintesi — reinterpretazione e proposte di testo

> **Nota.** Questo piano non modifica i `.tex`. Per ogni punto riporta la **reinterpretazione**
> della richiesta e il **testo che proporrei di aggiungere**, da valutare prima di scriverlo.

---

## 0. Il discorso del relatore, per esteso

### 0.1 Sul sincronizzatore, sul lessico e sui due piani

> "in una struttura classica, tipo archiettura di vanneschi PSN 50 anni fa, il "ready", quello
> con cui il ittente segnala che c'è pronto qualcosa non ha niente a che fare col fatto che il
> registro sia libero o non è libero, è roba mia ci scrivo quello che mi pare quando mi pare,
> quando ce l'ho scritto è un porblema mio se sovrascrivo o scrivo due volte sono cazzi miei
> però quando devo spedire faccio un setready cioè il contatore modulo 2 lo incremento di uno.
> dall'altra parte c'è un confrontatore modulo due uguale dove, ipotesi, inizialmente se da una
> parte ho zero di la devo avere un uno nel contatore e mi da false, quando lo metto ad 1 mi
> dice vero e allora posso fare un test su quella cosa li e contestualmente al fatto che il test
> lo trova uno leggo e subito dopo lo resetto ma resettare vuol dire che incremento di uno il
> mio contatore modulo due quindi se prima avevo un uno e mi arriva un uno e mi dice vero i ci
> metto +1 e mi dice falso e fintanto che tu non fai un altro setready dall'altra parte non
> procedi. Ora, come l'hai fatto te con il ready e l'ACK che sta nel limbo, il simulatore va
> benissimo. Nel codice verilog posso capire che tu aabbia avuto problemi e sia stato costretto
> a metetre due registri, ma quella cosa li la vorrei vedere, cioè vorrei vedere il codice
> verilog. Questo è il punto sul sincronizzatore ma va benissimo così come l'hai fatto per la
> tesi, purché nella stesura dall'inizio alla fine ci sono quelle due e tre cose che ti sei
> incarognito a metterle così e non c'è versi.
>
> La prima è quella del simulatore e della macchina simulata, no? più volte parli di simulatore
> di su e dell'implementazione di giu, va bene, però questa cosa dovresti chiarirla dall'inizio
> una volta per tutte ad esempio nel progetto logico quandokparlo di simulatore intendo il
> programma che simula con tanto di pezzo host e pezzo array e via dicendo, quando parlo di
> prove di scalabilità etc parlo del simulatore quando parlo dei nodi che sono emulati dentro il
> simulatore.
>
> Per il testo, ILPROGRAMMA HOST O HOST, ma come tantevaltre terminologie non standard, come
> pareggio sweep drenaggio etc. se le vuoi usare magari metti tra parentesi il significato con
> da qui in poi il punto di pareggio si intende... molto meglio e ti eviti domande in
> commissione di pignoli"

### 0.2 Su Jacobi

> "Su jacobi, jacobi è particolare, non è che sia una cosa cosi, jacobi lo usiamo tutti è uno
> dei metodi piu famosi peer la risoluzione dei sistemi di equazioni in maniera iterativa, va
> fatto in virgola mobile, non esiste uno che fa jacobi intero. però jacobi è l'esemplare per
> antonomasia di una comunicazione stencil perché chiappa i quattro vicini e poi mette il
> risultato. In realtà jacobi è la divisione per 5 dei quattro vicini e del valore esistente
> della cella, quindi non è vero che si divide per quattro. ovvero prendi da nord sud ovest est
> e sommi al valore attuale dividendo poi per 5 no? però è ragionevole quello che hai fatto, tu
> hai le cose intere e va bene, la divisione non ce l'avevi e va bene e il fatto è che quella
> cosa è li perché a noi ci interessa il pattern di comunicazione dello stencil, allora da
> qualche parte all'inizio della sezione di jacobi dove lo introduci per la prima volta dici
> jacobi nomrmlamente è una cosa che si fa con i numeri in virgola mobile (come del resto al
> moltiplicazione tra matrici) ma è un pattern ragionevole per rappresentare lo stencil e noi
> l'abbiamo fatto in questa forma qui siccome avevamo pensato di implementare solo la versione I
> senza RVIM, dunque sostanzialmente abbiamo fatto la divisione per quattro, ma è
> un'approssimazione del metodo vero"

### 0.3 Sui disegni (richiesta successiva)

> Vanno uniformati i disegni. Ogni cella segna la posizione con `(x,y)`, e sotto il suo ruolo o
> cosa contiene, come nella figura 3.1 — dove inoltre una nota a margine chiedeva di
> "specificare nella didascalia che (x,y) è la posizione nell'array che si può usare per
> differenziare il comportamento dei singoli nodi".

---

## 1. Sincronizzatore: cosa dicono davvero queste parole

Il modello che descrive è quello che NESO già implementa, con una corrispondenza puntuale:

| lui | NESO |
|---|---|
| "è roba mia, ci scrivo quello che mi pare quando mi pare" | `OUT` non è mai rifiutata; due `OUT` di fila sono due caricamenti dello stesso registro |
| "quando devo spedire faccio un setready, il contatore mod 2 lo incremento di uno" | `SETRDY` → `wp = wp ^ 1` |
| "dall'altra parte c'è un confrontatore modulo due" | `ch_isrdy` → `wp != rp` |
| "leggo e subito dopo lo resetto, ma resettare vuol dire che incremento di uno il mio contatore" | `ch_read_c` → `rp = rp ^ 1` |
| "fintanto che tu non fai un altro setready dall'altra parte non procedi" | il ritentativo su `ISRDY` |

**Conseguenza forte: `RESETRDY` non serve, e la §11 del documento di sintesi decade.**
Nell'email chiedeva *"o (meglio) devo avere un'altra istruzione RESETRDY"*; qui chiarisce che
nel modello classico il reset **è** l'incremento del contatore del lettore, cioè esattamente ciò
che `IN` fa già. Le proiezioni di costo (+17 % su matmul) sono lavoro che non va fatto.

**Cosa resta da fare in tesi:** dirlo. Non c'è oggi una frase che affermi *"il ready non ha
istruzione di azzeramento perché a riportarlo a falso è la `IN`, che incrementa il contatore del
consumatore"*. Restano validi i punti 1, 2 e 4 della §13 del documento, ora con il suo avallo
esplicito: riscrivere in termini di soli indicatori, abbandonando "pieno"/"vuoto" — che è
letteralmente ciò che apre il suo discorso ("non ha niente a che fare col fatto che il registro
sia libero o non libero").

### 1.1 Verifica del Verilog — **fatta**

Compilato ed eseguito fuori dal repo, nessun artefatto lasciato in `hw/`.

| controllo | esito |
|---|---|
| `tb_neso_channel` (5 scenari: reset, pubblicazione, backpressure, consumo, regressione `SETRDY` senza `OUT`) | **OK**, tutte le asserzioni passate |
| `tb_neso_if` (6 scenari: latenza di un hop fra due celle) | **OK** |
| `yosys synth -top neso_channel; stat` | **66 `$_DFFE_PN0P_`**, e 6 celle combinatorie: 1 `$_XOR_`, 1 `$_XNOR_`, 2 `$_ANDNOT_`, 2 `$_NOT_` |
| `yosys CHECK` | `Found and reported 0 problems` |
| corrispondenza 1:1 con [src/channel.h](src/channel.h) | verificata riga per riga: `out_reg`↔`data_next`, `pub_reg`↔`data`, `wp`/`rp` identici, `pub_ok = pub_en & iswrt`↔`ch_setrdy`, `rd_en && isrdy → rp<=~rp`↔`ch_read_c` |
| `verilator --lint-only` | **non eseguibile**: verilator non è installato. Non bloccante |

I 66 flip-flop e le 6 porte **coincidono esattamente** con la tabella di
[implementazione.tex:424-443](TemplateTesi/implementazione.tex#L424-L443) e con il commento di
[hw/neso_if.sv](hw/neso_if.sv). Il file è mandabile così com'è.

**Due cose che segnalerei prima di mandarlo**, entrambe nei commenti, non nel codice:

1. Le righe 11-12 di [hw/neso_channel.sv](hw/neso_channel.sv) descrivono lo stato come
   `pieno / leggibile` e `vuoto / scrivibile`, cioè nel vocabolario che ha appena chiesto di
   abbandonare. Sostituirei con la formulazione sua:
   > `wp != rp : il consumatore ha qualcosa da leggere (il comparatore dà vero)`
   > `wp == rp : il produttore può pubblicare (setready accettata)`
   > `Nessun bit di occupazione: lo stato è la DIFFERENZA fra i due contatori mod 2.`
2. Il commento di `neso_if.sv` (righe 27-33) anticipa già la sua obiezione naturale — che il
   flip-flop `rp` starebbe più propriamente nella cella consumatrice — e risponde che il conteggio
   non cambia, si sposta soltanto. **È il commento giusto e va lasciato dov'è**: è la prima cosa
   che cercherà, avendo in mente lo schema classico in cui il contatore del lettore è suo.

---

## 2. Simulatore contro macchina simulata

**Reinterpretazione.** Non chiede correzioni sparse — chiede una **dichiarazione unica e
anticipata**, dopo la quale ogni occorrenza si legge da sola. Le venti correzioni sparse censite
nella §15.1 del documento diventano allora verifiche di coerenza, non spiegazioni ripetute.

**Cosa aggiungerei**, in coda all'introduzione del capitolo 3
([progetto_logico.tex:5-12](TemplateTesi/progetto_logico.tex#L5-L12)):

> Prima di entrare nel merito conviene fissare due termini che il resto della tesi tiene
> distinti. Per *simulatore* si intende il programma che esegue la simulazione: comprende la
> parte che fa avanzare l'array ciclo per ciclo e il *programma host*, cioè il codice che
> alimenta e drena il perimetro, avvia l'esecuzione e ne raccoglie i risultati. Per *array*, e
> per *cella* o *nodo*, si intende la macchina simulata, che non esiste al di fuori del
> simulatore. La distinzione governa la lettura di tutte le grandezze che seguono: cicli,
> istruzioni, canali e protocollo appartengono alla macchina simulata; tempi di esecuzione,
> thread, speedup e prove di scalabilità appartengono al simulatore. Dove entrambe le letture
> sarebbero possibili, il testo dichiara di quale delle due si tratta.

L'ultima frase è quella che vale: è un impegno che poi va onorato nei punti già censiti
(`ecall` e terminazione, `memtest` e la SIGSEGV, i due contatori di misura, il cablaggio a
puntatori, il costo $O(R+C)$ del bordo, e la formulazione *"la parallelizzazione è della
simulazione di un singolo ciclo di clock"* in §6.1, §6.2 e §4.8).

---

## 3. Lessico e glosse

**Reinterpretazione.** Il criterio che dà è di *difesa in commissione*: un termine non standard
o lo si definisce alla prima occorrenza, o lo si toglie. Non chiede un glossario, chiede una
parentesi nel punto in cui il lettore incontra la parola per la prima volta.

### 3.1 `ospite` → `programma host` / `host`

Chiude la domanda rimasta aperta nella §23 del documento, dove sul cartaceo aveva scritto le due
direzioni opposte. **Vince `host`**, con `programma host` alla prima occorrenza. Sono ~40
occorrenze: `risultati.tex` 18, `progetto_logico.tex` 10, `implementazione.tex` 9,
`introduzione.tex` 2, `conclusioni.tex` 1, più i nodi e le didascalie delle figure. La
sostituzione va fatta su `\bospite\b`, senza toccare `ospita`/`ospitare`.

Prima occorrenza assoluta: [introduzione.tex:15](TemplateTesi/introduzione.tex#L15). Lì metterei:

> il *programma host* (da qui in poi *host*: il programma C che ospita la simulazione, l'unica
> parte del sistema che non è simulata)

e accorcerei di conseguenza la glossa già presente a
[progetto_logico.tex:280](TemplateTesi/progetto_logico.tex#L280), che diventa ridondante.

### 3.2 I termini nostri, glossati dove compaiono

| termine | prima occorrenza | stato oggi | cosa aggiungerei |
|---|---|---|---|
| `sweep` | [risultati.tex:32](TemplateTesi/risultati.tex#L32), in un'intestazione di tabella | nudo | glossa nel testo di §5.1 **prima** della tabella |
| `punto di pareggio` | [risultati.tex:88](TemplateTesi/risultati.tex#L88) | rinvio in avanti a §5.6, nessuna definizione | glossa alla prima occorrenza |
| `drenaggio` | [implementazione.tex:383](TemplateTesi/implementazione.tex#L383) | **già** in nota a piè di pagina | verificare solo che nessuna occorrenza la preceda |

Testo proposto per le due glosse, nella forma che ha indicato:

> *sweep* — da qui in poi si intende una serie di esecuzioni della stessa prova ripetuta su una
> successione sistematica di configurazioni: le forme della griglia, il numero di thread e il
> parametro di calcolo del programma.

> *punto di pareggio* — da qui in poi si intende la forma di griglia a partire dalla quale
> eseguire in parallelo il ciclo di clock del simulatore diventa più conveniente che eseguirlo
> in sequenza, cioè quella in cui lo speedup vale uno.

Restano nello stesso giro le voci di lessico già raccolte nella §22 del documento
(`immagine`→`copia`, `profondità uno`→`una posizione`, `fili`→`collegamenti da 1 bit`,
`deliberatamente povero`→`molto compatto`).

---

## 4. Jacobi

**Reinterpretazione.** Non contesta l'implementazione — dice due volte che va bene. Contesta che
il testo **presenti come Jacobi** una cosa che Jacobi non è, su due punti distinti che oggi la
tesi non separa:

1. Jacobi si fa in virgola mobile, e così pure la moltiplicazione di matrici. La scelta intera
   va dichiarata come scelta.
2. **Il metodo vero divide per cinque**, non per quattro: media dei quattro vicini *e del valore
   attuale della cella*. Questo la tesi oggi non lo dice affatto — a
   [risultati.tex:546](TemplateTesi/risultati.tex#L546) la media dei quattro vicini è enunciata
   come se fosse la definizione di Jacobi.

E dà anche la giustificazione da usare: quel programma sta lì **per il pattern di comunicazione**,
non per la convergenza numerica. Nessuna modifica a [asm/jacobi.s](asm/jacobi.s).

**Cosa aggiungerei**, in apertura di §5.3.5, prima di qualunque altra frase:

> Il metodo di Jacobi è uno dei procedimenti iterativi più noti per la risoluzione di sistemi di
> equazioni lineari, e si esegue normalmente in virgola mobile, come del resto la moltiplicazione
> di matrici della sezione precedente. Nella sua forma a cinque punti il nuovo valore di un
> elemento è la media del proprio valore attuale e di quelli dei quattro vicini ortogonali, cioè
> una divisione per cinque. Il programma descritto qui somma i soli quattro vicini e divide per
> quattro, su interi con segno a 32 bit, perché l'architettura implementata è la sola RV32I,
> priva tanto dell'unità in virgola mobile quanto dell'estensione M, e perché quattro è una
> potenza di due e la divisione si riduce a uno scorrimento. È dunque un'approssimazione del
> metodo, e come tale va letta. Ciò che il programma deve esercitare non è la convergenza
> numerica di Jacobi, ma il suo schema di comunicazione: lo stencil a cinque punti, in cui a ogni
> iterazione ogni cella scambia una parola con ciascuno dei quattro vicini ortogonali. Quello
> schema è identico nelle due versioni, ed è l'unica proprietà su cui poggiano le misure di
> questo capitolo.

**Ricadute** dentro la stessa sezione, se il capoverso entra:

- [risultati.tex:546](TemplateTesi/risultati.tex#L546) — *"sostituisce il proprio valore con la
  media dei quattro vicini"* va riferito alla forma semplificata appena definita, non presentato
  come Jacobi.
- [risultati.tex:574-583](TemplateTesi/risultati.tex#L574-L583) — oggi l'argomento è *"RV32I non
  ha la divisione, ma il divisore è quattro, potenza di due"*, e arriva prima che il lettore
  sappia che il divisore *dovrebbe* essere cinque. Va invertito: il divisore è quattro **per
  scelta**, ed è questa scelta a rendere possibile lo scorrimento. Da lì il paragrafo prosegue
  intatto, arrotondamento e punto fisso spurio compresi, che restano corretti e interessanti.
- [risultati.tex:618-622](TemplateTesi/risultati.tex#L618-L622) — la verifica dice già che il
  riferimento C *"non modella lo stencil, ma esegue la stessa espressione"*: coerente col
  capoverso nuovo, basta richiamarlo.
- §5.5.1 / §5.3.7, `matmul`: una subordinata sul fatto che anche la moltiplicazione di matrici
  si esegue normalmente in virgola mobile. È lui a suggerire l'accostamento.
- [risultati.tex:68](TemplateTesi/risultati.tex#L68), §5.1 — un rinvio a §5.3.5 dove compare "il
  campo iniziale dello stencil" e il contorno costante 64: è uno dei tre punti su cui aveva
  scritto *"non ci capisco proprio"*.

---

## 5. Uniformare i disegni — **applicato**

> Le cinque figure sono state modificate e la tesi ricompila senza riferimenti irrisolti né
> `Overfull \hbox`. Resta da confermare con lui la scelta `(r,c)` di §5.2.


**Reinterpretazione.** La figura 3.1 è lo standard: ogni cella porta la propria **posizione**, e
la posizione è l'unica cosa che distingue una cella dall'altra, dato che tutte eseguono la stessa
copia del programma. Le figure dei kernel hanno invece ognuna una convenzione diversa, e in due
casi la posizione sparisce del tutto. La sua richiesta è di rendere le sei figure leggibili con
la stessa chiave: **posizione sopra, ruolo o contenuto sotto**.

### 5.1 Stato attuale

| figura | etichetta della cella oggi | manca |
|---|---|---|
| 3.1 `fig:array` | `$(r,c)$` | la didascalia non dice cosa sia la coppia né a cosa serva |
| 3.2 `fig:cella` | "cella $(r,c)$" | nulla — è già coerente |
| 5.x `fig:broadcast` | il solo numero $r+c$ (passi di propagazione) | **la posizione** |
| `fig:chain` | il solo ruolo (`sorgente`, `inoltro`, `pozzo`) | **la posizione** |
| `fig:reduce` | **celle vuote** | posizione e ruolo |
| `fig:matmul` | `$C_{rc}$` | la posizione è implicita nel pedice, in una notazione diversa dalle altre |

### 5.2 `(x,y)` oppure `(r,c)`: una cosa da chiarire con lui

Ha detto `(x,y)`, ma nel progetto la coppia ha un nome preciso e un ordine preciso:
`a0 = r = riga`, `a1 = c = colonna`
([implementazione.tex:286-287](TemplateTesi/implementazione.tex#L286-L287), e
[src/risc.c:106](src/risc.c#L106)). Con `(x,y)` il lettore che venga dalla grafica leggerebbe
$x$ come ascissa, cioè come *colonna*, invertendo l'ordine rispetto a tutto il codice assembly
della tesi, dove `beqz a1` significa "sono nella colonna 0". **Proporrei di tenere `(r,c)`** —
che è già lo standard della figura 3.1, quella che lui indica come modello — e di sciogliere
l'ambiguità nella didascalia, che è esattamente ciò che la sua nota a margine chiedeva. Se
insiste su `(x,y)`, allora va dichiarato una volta che $x$ è la riga.

### 5.3 Lo schema uniforme che proporrei

Uno stile unico, definito una volta e riusato nelle sei figure:

```latex
cella/.style={draw, rounded corners=1pt, minimum size=1.3cm,
              font=\scriptsize, align=center}
% posizione sopra, ruolo o contenuto sotto
\node[cella] (n\r\c) at (...) {$(\r,\c)$\\[1pt] \textit{ruolo}};
```

Applicato alle singole figure:

| figura | riga superiore | riga inferiore |
|---|---|---|
| 3.1 `fig:array` | $(r,c)$ | — (è la figura della topologia: la posizione basta) |
| `fig:broadcast` | $(r,c)$ | il numero di passi $r+c$, che oggi è l'unica etichetta |
| `fig:chain` | $(0,0)$, $(0,1)$, …, $(0,C{-}1)$ | `sorgente`, `inoltro`, `pozzo` |
| `fig:reduce` | $(r,c)$ | `somma di riga`, e `raccolta` nell'ultima colonna, oggi solo ombreggiata |
| `fig:matmul` | $(r,c)$ | $C_{rc}$, cioè l'accumulo che resta fermo nella cella |

Sulla figura 3.2 non intervengo: dice già "cella $(r,c)$" e il suo contenuto è il corpo stesso
del disegno.

### 5.4 La didascalia della 3.1

È la nota a margine più esplicita del giro. Aggiungerei in coda alla didascalia esistente:

> La coppia $(r,c)$ è la posizione della cella nell'array, cioè riga e colonna. Poiché tutte le
> celle eseguono la stessa copia del programma, la posizione è l'unico dato che le distingue: è
> su di essa che il programma si dirama per differenziare il comportamento dei singoli nodi, ed è
> disponibile a ogni cella nei registri `a0` e `a1` (sezione~\ref{sec:registri}).

Una formula analoga, ridotta a una riga, andrebbe nelle didascalie delle figure dei kernel, dove
oggi la coppia comparirebbe senza essere mai stata spiegata in quel capitolo.

### 5.5 Le figure che mancano

Dalla §22 del documento: esistono i disegni per `broadcast`, `chain`, `reduce` e `matmul`;
**mancano** `prodcons`, `bordo`, `memtest`, `jacobi` e `pesante`, e per i primi due lo schizzo lo
ha disegnato lui a margine. Se si fanno, nascono già con lo schema di §5.3 — e `jacobi` in
particolare è la figura che vale di più, perché è lo stencil a cinque punti, cioè esattamente
ciò che il capoverso di §4 dichiara essere la ragione per cui quel programma esiste.

---

## 6. Aggiornare il documento di sintesi

[docs/canale_sintesi_confronto.md](docs/canale_sintesi_confronto.md) è il quaderno del confronto
e va chiuso, non lasciato in stato di preparazione:

- nuova **Parte III — esiti**, con le risposte riportate in §0 e la corrispondenza di §1;
- **§11 (`RESETRDY`)**: decaduto, il reset esiste già ed è `IN`;
- **§23 (`ospite` o `host`)**: chiuso, → `host`;
- **§13**: i punti 1, 2 e 4 confermati dal suo stesso modello; il 3 resta come miglioria;
- **§14/§25**, l'off-by-one su Jacobi ($33 \times 128 + 2 = 4226$ contro i **4227** riportati a
  [risultati.tex:796](TemplateTesi/risultati.tex#L796)): **ancora aperto**. Conviene chiuderlo
  ora che si mette mano al capitolo — o il prologo costa 3 cicli, o è sbagliato uno dei due
  fattori;
- **§1.1 di questo piano**: il Verilog è verificato, i 66 flip-flop sono confermati da yosys.

---

## Verifica, quando i testi si scriveranno

1. `cd TemplateTesi && latexmk -pdf Tesi_Corsi_Leonardo_656276.tex` — compilazione pulita e
   nessun riferimento irrisolto: glosse e rinvii nuovi ne introducono.
2. `grep -rn "\bospite\b" TemplateTesi/*.tex` → zero; conteggio di `ospita` invariato.
3. Rilettura mirata sul PDF: prima pagina del cap. 3 (blocco terminologico), §5.1 (glosse prima
   dell'uso), §5.3.5 (capoverso Jacobi prima del listato), e le sei figure a confronto sulla
   stessa schermata, che è il modo in cui si vede se sono davvero uniformi.
4. Ricontrollo dei 4227 cicli di Jacobi contro l'esecuzione reale, non contro il conteggio a mano.
