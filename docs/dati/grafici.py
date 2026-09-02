#!/usr/bin/env python3
"""
    Dalle misure grezze di scala.csv alle coordinate del grafico di tesi.

    Scrive TemplateTesi/dati_speedup.tex, che risultati.tex include dentro
    l'ambiente axis: i trenta valori del grafico non si trascrivono a mano,
    così non possono divergere dai dati.

    Si prende il MINIMO delle ripetizioni, non la media: la macchina non e'
    dedicata e un disturbo esterno puo' solo aggiungere tempo, mai toglierlo.
    Di ogni griglia si tiene lo speedup migliore fra le conte di thread.

    Uso: python3 docs/dati/grafici.py   (dalla radice del progetto)
"""

import csv
import os
from collections import defaultdict

SORGENTE = "docs/dati/scala.csv"
USCITA = "TemplateTesi/dati_speedup.tex"
STILI = {"pesante-p1": "peso uno", "pesante-p8": "peso otto", "pesante-p32": "peso trentadue"}


def leggi(percorso):
    """t_step minimo per ogni (kernel, lato della griglia, thread)."""
    minimi = defaultdict(lambda: float("inf"))
    with open(percorso) as f:
        for r in csv.DictReader(f):
            chiave = (r["kernel"], int(r["righe"]), int(r["thread"]))
            minimi[chiave] = min(minimi[chiave], float(r["t_step"]))
    return minimi


def speedup(minimi):
    """Per ogni kernel: [(lato, speedup migliore fra le conte di thread)]."""
    lati = sorted({k[1] for k in minimi})
    conte = sorted({k[2] for k in minimi})
    curve = {}
    for kernel in STILI:
        punti = []
        for lato in lati:
            seriale = minimi.get((kernel, lato, 1))
            if seriale is None:
                continue
            paralleli = [minimi[(kernel, lato, t)] for t in conte[1:]
                         if (kernel, lato, t) in minimi]
            if not paralleli:
                continue
            punti.append((lato, seriale / min(paralleli)))
        if punti:
            curve[kernel] = punti
    return curve


def scrivi(curve, percorso):
    righe = ["% Generato da docs/dati/grafici.py: non modificare a mano.",
             "% Rigenerare con 'make scala' oppure 'make grafici'.", ""]
    for kernel, punti in curve.items():
        righe.append("\\addplot coordinates {%% %s" % STILI[kernel])
        righe.append("    " + " ".join("(%d,%.2f)" % p for p in punti))
        righe.append("};")
        righe.append("\\addlegendentry{\\texttt{PESO} $= %s$}" % kernel.split("p")[-1])
        righe.append("")
    os.makedirs(os.path.dirname(percorso), exist_ok=True)
    with open(percorso, "w") as f:
        f.write("\n".join(righe))
    return len(curve)


def prova():
    """Controllo minimo: lo speedup e' seriale/parallelo, non l'inverso."""
    finti = {("pesante-p1", 64, 1): 4.0, ("pesante-p1", 64, 2): 2.0,
             ("pesante-p1", 64, 4): 1.0}
    curve = speedup(finti)
    assert curve["pesante-p1"] == [(64, 4.0)], curve
    print("prova: ok")


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1 and sys.argv[1] == "--prova":
        prova()
    else:
        quante = scrivi(speedup(leggi(SORGENTE)), USCITA)
        print("scritto %s: %d curve" % (USCITA, quante))
