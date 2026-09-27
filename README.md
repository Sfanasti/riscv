# NESO

Simulatore di un array sistolico bidimensionale basato su RISC-V.
Tesi di laurea triennale in Informatica, Università di Pisa.
Candidato: Leonardo Corsi. Relatore: Marco Danelutto.

NESO simula una griglia R×C di celle. Ogni cella è un processore RV32I con
memoria privata (16 KB di default) e comunica solo con i quattro vicini (nord,
est, sud, ovest) attraverso canali monodirezionali a un posto. L'interprete esegue anche
l'estensione M, usata solo da `matmul.s`. Il tempo è globale: a ogni
ciclo ogni cella attiva esegue una istruzione. Quattro istruzioni custom
(`IN`, `OUT`, `ISRDY`, `SETRDY`, opcode custom-0) danno accesso ai canali.
Il canale è modellato anche in SystemVerilog.

## Requisiti

- `gcc` con OpenMP e `make`
- `binutils-riscv64-unknown-elf` (serve solo l'assembler)
- per `hw/`: `iverilog`, `verilator`, `yosys`, `gtkwave`
- per i grafici di `make scala`: `python3`

Su Ubuntu:

```sh
sudo apt install build-essential binutils-riscv64-unknown-elf \
                 iverilog verilator yosys gtkwave
```

## Uso

```sh
make                                  # compila build/neso
make test                             # suite completa
make run P=broadcast R=3 C=4 N=400    # un kernel su griglia 3x4, max 400 cicli
make step P=prodcons R=1 C=2 N=40     # un ciclo per INVIO
make -C hw                            # testbench del canale e dell'interfaccia
make -C hw lint                       # verilator --lint-only
```

La memoria privata si fissa a tempo di compilazione con `MEM_SIZE`, in parole
(default 4096, cioè 16 KB), da passare sia alla build sia ai test, che
ricompilano i sorgenti. Esempio con 1 KB:

```sh
make clean
make      CFLAGS="-Wall -Wextra -std=c11 -Isrc -O2 -fopenmp -DMEM_SIZE=256"
make test CFLAGS="-Wall -Wextra -std=c11 -Isrc -O2 -fopenmp -DMEM_SIZE=256"
```

`test-mem` presuppone una memoria di al più 16 KB.

`make run` stampa lo stato di ogni cella a fine esecuzione. L'intestazione di
ogni `.s` in `asm/` indica cosa fa il kernel, il risultato atteso e i parametri.
Le altre variabili e i target di misura sono descritti in testa al `Makefile`.

## Struttura

| cartella | contenuto |
| --- | --- |
| `src/` | simulatore: interprete (`risc.c`), canale (`channel.h`), griglia (`grid.c`), caricatore ELF, `main.c` |
| `asm/` | kernel RISC-V: produttore/consumatore, catena, broadcast, riduzione, Jacobi, prodotto di matrici, kernel sintetico |
| `tests/` | harness in C che eseguono i kernel e ne verificano il risultato, più il test del canale |
| `hw/` | modello RTL del canale e dell'interfaccia di cella, con testbench |
| `docs/dati/` | misure in CSV usate nella tesi, descritte in `docs/dati/README.md` |
