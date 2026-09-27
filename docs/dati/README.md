# Misure

CSV generati dal Makefile, una riga per esecuzione. Non si modificano a mano:
se un numero non torna si cambia il codice e si rigenera. Per leggerli:
`column -t -s, <file>.csv`. I risultati sono discussi nel capitolo 5 della tesi.

| file | target | contenuto |
| --- | --- | --- |
| `catena.csv` | `make dati` | `prodcons.s` e `chain.s`, con e senza controllo di flusso |
| `broadcast.csv` | `make dati` | `broadcast.s` e `memtest.s` |
| `bordo.csv` | `make dati` | `bordo.s` |
| `reduce.csv` | `make dati` | `reduce.s` |
| `jacobi.csv` | `make dati` | `jacobi.s` |
| `matmul.csv` | `make dati` | `matmul.s` |
| `pesante.csv` | `make dati` | `pesante.s` |
| `tempi.csv` | `make tempi` | tempi dei kernel della suite per 1, 2, 4, 8, 16 thread |
| `scala.csv` | `make scala` | tempi di `pesante.s` fino a 256×256 |
| `memoria.csv` | `make memoria` | tempi a un thread al crescere della griglia |
| `controllo.csv` | `make controllo` | tempi a parità di cicli con `PESO` diverso |

`grafici.py` (lanciato da `make scala`) ricava da `scala.csv` le coordinate
del grafico di tesi, in `TemplateTesi/dati_speedup.tex`.

## Colonne comuni

| colonna | significato |
| --- | --- |
| `kernel` | programma in `asm/`, senza estensione |
| `righe`, `colonne` | forma della griglia |
| `cicli` | passi di `grid_step` fino all'ultimo core fermo; in un ciclo ogni core attivo esegue una istruzione |
| `ritentativi` | `SETRDY` che hanno restituito 0 (canale di uscita pieno), su tutte le celle |
| `attese` | `ISRDY` che hanno restituito 0 (canale di ingresso vuoto), su tutte le celle |

`ritentativi` e `attese` contano istruzioni, non cicli: il costo in cicli di
un'attesa dipende dalla lunghezza del loop del kernel.

## Colonne per file

**catena.csv** — `ritardo` = `RITARDO`, cicli di attesa del consumatore prima
di ogni lettura. `quanti` = `Q`, valori spediti. `somma` = `s1` dell'ultima
colonna, attesa `Q*(Q+1)/2`. `backpressure` = `si` protocollo normale, `no`
con `NOBP=1`: lì una somma diversa dall'attesa è il risultato, non un guasto.

**broadcast.csv** — `valore` = `s1` verificato su ogni cella (per `memtest`
il pattern `0x11223344`).

**bordo.csv** — `usciti` = valori raccolti dal bordo sud con `grid_pop`, uno
per colonna.

**reduce.csv** — `totale` = somma di r+c su tutte le celle.

**matmul.csv** — `k` = lunghezza del prodotto interno. `spinte_rifiutate` =
`grid_push` che hanno trovato il canale di bordo pieno. I cicli seguono
`19 + 16(k−1) + 10(R+C−2)` su tutte le righe.

**jacobi.csv** — `bordo`, `iter`, `seme` = contorno, iterazioni, campo
iniziale (0 = interno a zero, 1 = r+c). `usciti` = valori drenati dal
perimetro. `k_finale` = prima iterazione in cui il riferimento in C smette di
cambiare. `delta` = variazione massima all'ultima iterazione; `0` significa
punto fisso raggiunto.

**pesante.csv** — `peso` = round di xorshift per iterazione. `bordo`, `iter`,
`usciti` come in jacobi.csv. `ritentativi` e `attese` sono zero: le celle
restano in lockstep. `cicli` è arrotondato a multipli di 64, il passo con cui
l'harness controlla la terminazione.

**tempi.csv, scala.csv, memoria.csv, controllo.csv** — righe scritte dal
cronometro di `src/grid.c` (variabile `TEMPI`), non da `time`.

| colonna | significato |
| --- | --- |
| `thread` | `OMP_NUM_THREADS` |
| `t_tot` | secondi dal primo all'ultimo `grid_step`, compreso l'I/O di bordo seriale dell'harness |
| `t_step` | secondi dentro `grid_step`, la parte parallela |
| `seriale` | `1 - t_step/t_tot`, frazione seriale misurata |

Il campo `kernel` porta i parametri: `pesante-p32` in scala.csv,
`pesante-p1-i62` in controllo.csv. In memoria.csv il costo per cella-ciclo è
`t_step / (cicli · righe · colonne)`, calcolato in analisi.

## Lettura dei tempi

- In `scala.csv` ogni punto è ripetuto nove volte, interlacciando le
  condizioni; si usa il minimo, perché il rumore aggiunge solo tempo.
- Non rigenerare `tempi.csv` con uno sweep ridotto (`THREAD='1 8'`): il file
  ha lo stesso nome ma meno punti.
- Macchina: portatile non dedicato, 6 P-core con SMT e 8 E-core, governor
  `powersave`. Sopra i 12 thread entrano gli E-core. Il valore assoluto dello
  speedup cambia fra sessioni; la forma delle curve e la posizione del
  pareggio no.
