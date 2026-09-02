# Misure

Rigenerate con `make dati`, lette a occhio con `column -t -s, <file>.csv`.
Una riga per esecuzione, gli sweep sono quelli di `make test` (`RITARDI`,
`COLONNE`, `FORME`, `SEMI` nel Makefile). Non si scrivono a mano: se un numero
qui non torna, si cambia il codice e si rigenera.

## Colonne comuni

| colonna | cosa misura |
|---|---|
| `kernel` | nome del programma in `asm/`, senza estensione |
| `righe`, `colonne` | forma della griglia |
| `cicli` | passi di `grid_step` dal primo all'ultimo core fermo. È il clock globale dell'array, **non** un tempo e non un conteggio di istruzioni: in un ciclo ogni core vivo esegue una istruzione |
| `ritentativi` | quante `SETRDY` hanno restituito 0, cioè hanno trovato il canale di uscita ancora pieno. Sommate su tutte le celle |
| `attese` | quante `ISRDY` hanno restituito 0, cioè hanno trovato il canale di ingresso vuoto. Sommate su tutte le celle |

`ritentativi` e `attese` contano **istruzioni fallite, non cicli persi**: un giro
di respin costa quanto è lungo il loop del kernel (in `prodcons.s` cinque
istruzioni per ritentativo, quattro per attesa). Per il costo in cicli si
guarda `cicli`, non questi due. Li conta il simulatore (`grid_spin`, contatori
in `RISC_V`), quindi ci sono anche per i kernel che non tengono il conto in un
registro; dove il kernel lo tiene (`s3`/`s4` di `prodcons.s` e `chain.s`)
`test_catena` verifica che i due conteggi coincidano.

Il totale non dice *dove* si aspetta: per il collo di bottiglia serve il dato
per cella (`make test-broadcast` senza `CSV=1` stampa la mappa di `s4`).

## Colonne per file

**catena.csv** — `prodcons.s` e `chain.s`, stesso harness.
`ritardo` = `RITARDO`, cicli sprecati dal consumatore prima di ogni lettura.
`quanti` = `QUANTI`, valori spediti dalla sorgente. `somma` = `s1` dell'ultima
colonna, attesa `quanti*(quanti+1)/2` = 15.
`backpressure` = `si` protocollo normale, `no` modalità `NOBP=1` (canale senza
handshake): le righe con `no` sono il termine di paragone, e una `somma` diversa
da 15 lì è il risultato, non un guasto.

**broadcast.csv** — `broadcast.s` e `memtest.s`. `valore` = contenuto di `s1`
verificato su **ogni** cella (per `memtest` è il pattern 0x11223344).

**bordo.csv** — `usciti` = valori raccolti con `grid_pop` dal bordo sud, uno per
colonna se nessuno si è perso o duplicato.

**reduce.csv** — `totale` = risultato della riduzione, atteso `somma su (r,c) di r+c`.

**matmul.csv** — `k` è la lunghezza del prodotto interno (colonne di A = righe di
B); la griglia R×C è la forma del risultato, una cella per elemento.
`spinte_rifiutate` = quante `grid_push` hanno trovato il canale di bordo ancora
pieno, sommate su tutte le righe e le colonne: è la backpressure vista **dal
lato host**, cioè quanto l'array ha frenato chi lo alimenta. Non ha un
equivalente negli altri kernel, dove il bordo o è costante o si spinge una
volta sola.
Il costo misurato segue `cicli = 19 + 16(k−1) + 10(R+C−2)`: il numero di celle
non compare, e le tre costanti dipendono dalla lunghezza del loop di
`matmul.s` — se cambia il kernel vanno rimisurate.

**tempi.csv** — non viene da `make dati`: si rigenera con `make tempi`, che
ripete lo stesso sweep per ogni kernel di `KERNELS` e per ogni valore di
`THREAD`, cioè sei kernel per cinque conte di thread, 400 righe. Misura un tempo
vero e non un conteggio, quindi cambia da un'esecuzione all'altra: si legge per
ordini di grandezza e rapporti, non alla cifra.

Attenzione a non rigenerarlo con lo sweep ristretto (`make tempi THREAD='1 8'`
e simili): il file che ne esce ha lo stesso nome e lo stesso formato ma copre
due punti invece di venticinque, e le medie che se ne ricavano non sono le
stesse.
Lo cronometra `src/grid.c` quando `TEMPI` punta a un file, non `time` della
shell, che conterebbe anche il caricamento dell'ELF e il riferimento in C degli
harness.

| colonna | cosa misura |
|---|---|
| `thread` | `OMP_NUM_THREADS` del run; `1` anche per una build senza `-fopenmp` |
| `t_tot` | secondi dal primo `grid_step` all'ultimo: comprende l'I/O di bordo e il controllo di terminazione dell'harness, che sono **seriali** |
| `t_step` | secondi spesi dentro `grid_step`, cioè la sola parte parallelizzata |
| `seriale` | `1 - t_step/t_tot`, la frazione seriale **misurata**: il tetto di Amdahl del simulatore |

Il confronto parallelo/seriale è il rapporto fra righe con lo stesso `kernel` e
la stessa forma e `thread` diversi. Sulle griglie di questi sweep il rapporto è
**minore di 1**: a 12×12 Jacobi passa da 0,0137 s con un thread a 0,0780 s con
otto, cioè va quasi 6× più lento. Non è un guasto ed è il risultato atteso:
`grid_step` apre due regioni parallele per ciclo e a 12×12 il lavoro di un ciclo
è più piccolo del costo delle due barriere. Il pareggio misurato è a 40×40
(vedi **scala.csv**), che è oltre lo sweep `FORME`.

`seriale` resta sotto il 15 % su tutte le righe tranne le due esecuzioni più
corte in assoluto, `chain` su 1×5 (70 cicli, 23,7 %) e `matmul` su 12×12
(335 cicli, 19,1 %): lì il run dura decine di microsecondi e il costo fisso
dell'harness non è più trascurabile. Dove c'è abbastanza lavoro da misurare il
collo di bottiglia è dentro `grid_step` e non nell'harness. Si ribalta di nuovo
sulle griglie grandi, dove il controllo di terminazione `O(n)` a ogni ciclo
diventa il termine dominante, ed è il motivo per cui `test_pesante.c` lo esegue
una volta ogni 64 cicli invece che a ogni ciclo.

Dove cade davvero il pareggio non si legge qui ma in **scala.csv**, che estende
lo sweep fino a 256×256.

**jacobi.csv** — `bordo`, `iter`, `seme` sono i parametri (contorno di Dirichlet,
iterazioni, campo iniziale: 0 interno freddo / 1 campo r+c).
`usciti` = valori drenati dal perimetro. `k_finale` = prima iterazione in cui il
campo di riferimento smette di cambiare. `delta` = variazione massima
all'ultima iterazione: `0` significa punto fisso raggiunto, quindi `k_finale` è
una convergenza e non solo l'ultima iterazione eseguita.

**pesante.csv** — `pesante.s`, il kernel sintetico. `peso` = round di
mescolamento per iterazione, cioè quante istruzioni di calcolo stanno fra due
comunicazioni: a `peso=32` sono 288 contro le 16 di canale, contro le 6 contro
16 di Jacobi. `bordo`, `iter`, `usciti` come in jacobi.csv.
`ritentativi` e `attese` sono **zero su ogni forma**, e non è un difetto della
misura: tutte le celle eseguono lo stesso numero di istruzioni fra due
comunicazioni, quindi restano in lockstep e nessuna trova mai un canale nello
stato sbagliato. È il kernel con il profilo di comunicazione più regolare
possibile, che è esattamente ciò che serve per misurare il costo della
parallelizzazione senza che lo spin lo sporchi.
`cicli` è arrotondato per eccesso a un multiplo di 64: l'harness controlla la
terminazione una volta ogni 64 cicli invece che a ogni ciclo, perché quella
scansione è seriale su tutte le celle e a 65 k celle nasconderebbe lo speedup
che il kernel serve a misurare.

**scala.csv** — come `tempi.csv` non viene da `make dati`: si rigenera con
`make scala`. Stesse colonne di `tempi.csv`, ma sweep diverso e per una domanda
diversa: **dove** cade il pareggio della parallelizzazione, e come si sposta al
crescere del lavoro per cella. Un solo kernel (`pesante.s`), dodici forme da 8×8
a 256×256, cinque conte di thread, tre valori di `PESO`, nove ripetizioni per
punto, 1620 righe.
Il campo `kernel` porta il peso: `pesante-p1`, `pesante-p8`, `pesante-p32`.

Le forme 40×40 e 56×56 non sono di riempimento: senza di loro lo sweep salta da
32×32 a 48×48 e il pareggio resta localizzato solo a quell'intervallo. Con
40×40 nello sweep il pareggio si legge direttamente, e cade lì.

In analisi si prende il **minimo** delle nove ripetizioni, non la media né la
mediana: la macchina non è dedicata e il rumore può solo aggiungere tempo, mai
toglierlo, quindi il minimo è la stima più vicina al costo vero. Su qualche
punto lo scarto fra la ripetizione più veloce e la più lenta supera il 100 %, e
lì la media sarebbe fuorviante.

Le condizioni sono **interlacciate**: la ripetizione è il ciclo più esterno e
`PESO` il più interno. Non è una formalità. Un primo tentativo eseguiva i tre
valori di `PESO` in blocchi separati, e ogni blocco cadeva in un momento diverso:
una deriva della macchina si era travestita da effetto di `PESO`, con uno scarto
apparente del 65 % che una rimisurazione controllata ha smentito. Interlacciando,
la deriva colpisce tutte le condizioni allo stesso modo.

Il risultato, letto sui minimi: **il pareggio è a 40×40 per tutti e tre i valori
di `PESO`**, cioè fra due forme contigue dello sweep (a 32×32 le tre curve danno
0,83× 0,94× 0,93×, a 40×40 danno 1,11× 1,26× 1,38×). Sotto il pareggio la
perdita è severa (a 8×8 il parallelo arriva a un nono della velocità seriale),
ed è il regime in cui cade tutto `tempi.csv`, che si ferma a 12×12.

Nota storica: con lo sweep precedente, che saltava da 32×32 a 48×48, si leggeva
"pareggio a 48×48". Non era sbagliato ma impreciso: 48×48 era solo la prima
forma **misurata** sopra il pareggio. Le due forme aggiunte lo hanno collocato a
40×40, dentro lo stesso intervallo.

L'assenza di un effetto di `PESO` non è un difetto della misura, è il risultato,
e ha una ragione strutturale: **un ciclo è per costruzione una istruzione per
cella**, quindi `grid_step` fa lo stesso lavoro ogni ciclo qualunque cosa le
celle eseguano. Il costo delle due barriere rapportato al lavoro di un ciclo
dipende solo da R·C. Un kernel a maggiore intensità di calcolo non rende più
pesante il ciclo, ne esegue di più, e il numero di cicli si semplifica nel
rapporto fra tempo seriale e tempo parallelo. Il controllo sta in
**controllo.csv**.

Attenzione: in `scala.csv` è fisso `ITER` e non il numero di cicli, quindi
`PESO=32` esegue trentadue volte i cicli di `PESO=1` e ammortizza su più cicli
il costo di creazione dei thread. È il motivo per cui fra 40×40 e 64×64 le tre
curve restano ordinate per `PESO` con uno scarto fino al 25 %. Per il confronto
pulito serve `controllo.csv`, dove i cicli sono pari per costruzione.

Tre avvertenze di lettura, tutte severe:

1. La macchina ha 6 P-core con SMT e 8 E-core: **sopra i 12 thread** parte del
   lavoro finisce sui core lenti, e la colonna a 16 thread non è confrontabile
   linearmente con le altre.
2. È un portatile con governor `powersave` e non è dedicato: **il valore
   assoluto dello speedup non è riproducibile fra sessioni**. Tre sessioni a
   distanza di ore e di giorni hanno dato picchi di 8,5×, 7,3× e 3,7×. Quello
   che si è ritrovato identico è la collocazione del pareggio, la severità della
   perdita sotto di esso, l'andamento della frazione seriale e l'indipendenza da
   `PESO`. I numeri vanno letti come forma della curva, non alla cifra.
3. Il passo fra celle contigue è di 16 KB, e il degrado che ne segue si misura
   a parte in **memoria.csv**.

---

**memoria.csv** — si rigenera con `make memoria`. Risponde a una domanda sola:
quanto costa simulare una istruzione al crescere della griglia **a un solo
thread**, dove OpenMP non entra in gioco e resta solo la località. Stesse colonne
di `tempi.csv`. Il costo per cella-ciclo è `t_step / (cicli · righe · colonne)`,
si calcola in analisi e non sta nel file.

`ITER` è accoppiato alla forma (`MEM_FORME` nel Makefile è una lista
`forma:iterazioni`) perché ogni run duri fra uno e due secondi: senza, una
griglia piccola durerebbe millisecondi e il confronto misurerebbe anche
l'avviamento. Se si cambia `MEM_PESO` i valori di `ITER` vanno rifatti.

Risultato: da 15,9 ns su 32×32 a 83,4 ns su 256×256, **più che quintuplica** per
sola località, con il minimo su 32×32, che è l'ultima forma che entra nei 24 MB
di L3. Il fattore dipende dalla sessione (in sessioni diverse è stato 3× e 5×),
la posizione del minimo e la monotonia oltre di esso no. È l'argomento della
memoria parametrica, misurato invece che previsto.

**controllo.csv** — si rigenera con `make controllo`. `PESO=1` con `ITER=62` e
`PESO=32` con `ITER=8` eseguono **entrambi 2624 cicli**, pur differendo di
trentadue volte nelle istruzioni di calcolo per comunicazione. Il target
riasserisce la parità dei cicli su ogni riga e fallisce se non vale, perché
senza quella il confronto non significa niente. Il campo `kernel` porta
entrambi i parametri: `pesante-p1-i62`, `pesante-p32-i8`.

Risultato, speedup a 8 thread sui minimi di cinque ripetizioni: su 32×32 0,78×
contro 0,77×, su 48×48 1,91× contro 2,20×. Su 32×32 coincidono; su 48×48 i
minimi distano il 15 % ma le cinque ripetizioni delle due condizioni si
sovrappongono, quindi la misura non risolve la differenza. **A parità di cicli
l'intensità di calcolo non produce un effetto misurabile.**
