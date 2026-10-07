# Contesto del progetto NESO (memoria di Claude Code)


---
<!-- user_context.md -->
---
name: user-context
description: Who the user is and the academic context of the riscv/NESO project
metadata: 
  node_type: memory
  type: user
  originSessionId: 09a3907f-9d9b-475e-ba9d-3ec560148da7
  modified: 2026-07-29T10:43:03.554Z
---

The user is a student building **NESO**, a RISC-V array simulator, as an academic project supervised by a professor ("il relatore"). The codebase extends **stella**, a pre-existing single-core RV32I(+M) instruction-set-simulator from a prior thesis (Andolfo), rather than starting from scratch.

The user is comfortable with C and systems concepts but treats this conversation as a learning session as much as a coding session — they frequently ask "why" a design choice is made (e.g. why `static inline`, why XOR-toggle, why malloc vs calloc) and want the reasoning, not just the code. See [[feedback-collaboration-style]].

They communicate with their professor by email and have asked for help drafting/proofreading those emails when a design decision needs external sign-off.

---
<!-- feedback_code_style.md -->
---
name: feedback-code-style
description: "NESO house style agreed 2026-07-30 — comment form, braces everywhere, Italian prose with correct accents"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: e98b82df-3f1e-49fa-aa5b-8c37616c49b0
  modified: 2026-09-13T12:43:04.120Z
---

Style the user asked for across all of `src/`, `tests/`, `asm/` and the Makefile
(2026-07-30), applied to the whole repo and verified with `make test`:

- **Comments**: `/* txt */` on one line; multi-line as `/*` alone, body indented
  one level, `*/` alone. No `//` anywhere.
- **Braces**: every `if`, `else`, `for` and `while` takes braces, including
  single-statement bodies. Two deliberate exceptions he kept: the tabular
  `case 0xN: if (...) { branch_taken = 1; }` rows in `core.c`'s BRANCH switch
  stay on one line, and an empty `while` body gets a `{ /* ... */ }` that says
  the emptiness is intended.
- **Italian**: accented vowels, never `vocale'`. Acute on `né`, `perché`,
  `finché`, `affinché`, `poiché`, `purché`; grave on `è`, `così`, `più`, `può`,
  `già`, `però`, `ciò`, `lì`, `là`. Capital `È` at the start of a sentence, but
  lowercase after a colon and mid-sentence — check the previous line before
  changing one.

- **Comment content** (2026-09-13, when the sources went into the thesis
  appendices): "il più asciutti possibile e solo dove servono, non spiegoni sul
  codice". Keep invariants, encodings, function contracts and a non-obvious why
  in one or two lines. Cut development history ("costava il 58 %", "non sono
  più…"), design debate already argued in the thesis, and paraphrase of the
  code. Put the contract in the `.h` and the why in the `.c`; keep lines under 80
  characters so the appendix listings do not wrap. The `.sv` files follow the
  same rules, with real accents too.

**Why:** it is a thesis deliverable, read by a relatore, so prose and code both
get proofread. `src/channel.h`, `risc.h`, `grid.*` and `hw/neso_*.sv(h)` are
printed verbatim in the appendices, and two `risc.c` excerpts are printed with
fixed `firstline` values in `TemplateTesi/appendici.tex`.

**How to apply:** when touching any file here, match this without being asked.
Any edit to `risc.c` that shifts lines must re-align the two excerpt ranges in
`appendici.tex`.
Body indentation of a re-braced block is *recomputed* to one level from its
header, not added on top of the hanging indent it already had. See
[[project-accent-pass-gotcha]] for what breaks when this is automated.

---
<!-- feedback_collaboration_style.md -->
---
name: feedback-collaboration-style
description: How this user likes to work through implementation tasks — mostly writes code themselves
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 09a3907f-9d9b-475e-ba9d-3ec560148da7
  modified: 2026-09-09T08:15:07.427Z
---

Default mode: the user writes the actual code themselves, turn by turn, and uses me for design proposals, code sketches (given as illustrative snippets, not applied edits), reviews, and bug-catching after they've written something. When I made an unrequested Edit once (rewriting a couple of lines in core.c) they explicitly rejected it: "dimmi solo come aggiornare senza fare te l'update."

**Why**: this is their thesis/project code, they want to be the one typing it and understanding every line — the "why" matters more to them than speed.

**How to apply**: when they describe a problem or ask "come lo faccio", default to explaining + a short illustrative code sketch, not calling Edit/Write. Only make direct file edits when they explicitly ask ("aggiorna te main.c", "fallo tu", "si aggiorna [file]") — this does happen (e.g. they had me write main.c + update the theory doc directly in one late-session request when short on time/session length). After any such direct edit, still run a real compile/smoke-test check and report findings plainly, including bugs — they want issues flagged, not glossed over (e.g. they explicitly asked me to "controlla" their code multiple times and appreciated catching a real bug where ISRDY/OUT logic had been swapped).

**Thesis prose (`TemplateTesi/*.tex`) is stricter than code**: for wording changes he wants the revised text written out in the chat, never applied with Edit — "mi devi sempre solo scrivere qui il testo" (2026-09-09), after rejecting three Edit calls on caption and paragraph wording. He pastes it in himself. The exception is mechanical work he has explicitly told me to do (the tikz figure pass, the global `contorno` → `valore di bordo` substitution): there, edit and compile. See [[feedback-relatore-writing-style]] for the register those proposals must be in, and give them as: short list of problems first, then the complete revised passage.

Answers should generally be concise/"sintetico" — they've asked for brevity explicitly more than once. Don't over-ask: when a design fork is real but the tradeoff is clear, give a direct recommendation and ask for a quick confirmation rather than a long menu of options.

---
<!-- feedback_english_technical_terms.md -->
---
name: feedback-english-technical-terms
description: "For established technical terms he prefers the English word (in \\emph) over an Italian calque — \"working set\", not \"stato caldo\""
metadata: 
  node_type: memory
  type: feedback
  originSessionId: f82d218d-57c2-4adf-ab85-6966b6382a79
  modified: 2026-09-13T13:50:05.168Z
---

On 2026-09-13 he replaced "stato caldo" with *working set* throughout the thesis: "preferisco usare la terminologia inglese". He then asked for a list of every Italian term calqued from English, to decide case by case which ones to switch.

**Why:** a calque the reader has never seen ("stato caldo", "fallimenti della cache", "buffer di traduzione") is less clear than the established English term. The relatore reads the standard terms fluently.

**How to apply:**
- When a concept has an established English name in the field (working set, TLB, cache miss, memory wall, overflow, offset, target, toolchain), propose the English term in `\emph{}`, glossed at first use (see [[feedback-gloss-terms-at-first-use]]).
- Keep genuine Italian technical vocabulary: fronte, stallo, banda, regione parallela, barriera, sezione critica, grado di asincronia (the latter comes from the relatore's school).
- Keep names that mirror identifiers in the code or data, such as `ritentativi` and `attese`.
- He decides the replacements; propose them, do not apply them unasked.

---
<!-- feedback_gloss_terms_at_first_use.md -->
---
name: feedback-gloss-terms-at-first-use
description: "relatore's binding rule — every non-standard term carries its meaning inline at its FIRST use, and first use may be a table header or caption, not the running text"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: f82d218d-57c2-4adf-ab85-6966b6382a79
  modified: 2026-09-09T20:41:09.724Z
---

Danelutto ruled that non-standard terms in the NESO thesis (*sweep*,
*pareggio*, *drenaggio*, *host*, …) must carry their meaning **in
parentheses at first use**, in the form "da qui in poi il punto di pareggio
si intende…". Applied 2026-09-09 to *sweep*: the gloss was a footnote at
`risultati.tex:62` while the word first appeared three times earlier, in a
table column header and its caption — and the mechanism it names (the
Makefile targets `dati` and `tempi`, which run every program once per
parameter combination) was already described, unnamed, in
`strumenti.tex` §2.2. The fix was to name it there, where the thing is
described, and reduce chapter 5 to plain use.

**Why:** a footnote three uses late reads as an afterthought, and the
reader who met the word in a table header has already had to guess. He also
prefers the gloss inline over a footnote — a footnote signals the term is
peripheral, and these terms are not.

**How to apply:**

- Grep the whole thesis for the term before deciding where the gloss goes;
  first use is often inside a `\caption{}`, a table header or a figure
  label, which reading the running text alone will miss.
- Put the definition where the **mechanism** is first described, not where
  the word first happens to be convenient, then let later chapters just use
  it. A forward reference to the section that details it costs one clause.
- Prefer inline parenthetical + "da qui in poi" over `\footnote{}`.
- Don't leave two glosses: after adding one, check the later occurrence
  isn't still saying the same thing.
- Watch for the reverse case too — a word used in two senses in different
  chapters is worse than an unglossed one; see [[feedback-two-planes-rule]]
  for the *piano* collision.

---
<!-- feedback_relatore_writing_style.md -->
---
name: feedback-relatore-writing-style
description: "Danelutto's standing note on thesis prose style — more technical register, less colloquial, no meta-narration — applies to all future chapters, not just the one corrected"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 01770a32-8b58-45ee-a59f-6daad15cb86a
  modified: 2026-08-14T14:51:32.381Z
---

The relatore (Marco Danelutto) returned `Tesi_capitoli_1-3.pdf` (chapters
1-3) with inline comments and one explicit general note: **too colloquial,
needs more technical/precise vocabulary ("gergo tecnico"), and "tienine conto
per le prossime parti che devi scrivere"** — a standing style rule for
chapters 4-6, not a one-off fix for chapter 1.

**Why:** thesis prose was drifting toward explanatory/conversational framing
(stating the thesis's own claims about itself, e.g. "la proposta è la più
piccola possibile", "per dimostrarlo serviva qualcosa su cui misurare")
instead of technical argument. He specifically flagged this as
**metanarrazione e discorsi fini a se stessi** — sentences that comment on the
writing/argument rather than making a technical claim.

**How to apply**, concretely, from the chapter-1 correction:

1. **Motivate before proposing.** Don't state "this thesis removes assumption
   X" without first describing the naive/baseline approach and the concrete
   failure it causes. Correct order: naive approach → the problem it causes →
   the thesis's solution. (Applied in `introduzione.tex`: naive channels
   without synchronization → timing-dependent silent corruption → the
   ready-register principle.)
2. **Cut self-referential framing.** No "la proposta è la più piccola
   possibile", no "per dimostrarlo serviva qualcosa su cui misurare" — these
   describe the thesis instead of the system. State what was built and why it
   works, not that it's minimal/elegant/necessary.
3. **Precision over vague colloquial phrasing.** E.g. "sbagliato in silenzio,
   perché nessun componente si accorge di niente" → "produce un risultato
   numericamente definito ma errato, senza che alcun componente rilevi
   l'anomalia" — names *what* is wrong (a numerically well-defined but
   incorrect result) instead of a general "sbagliato in silenzio".
4. **No itemized lists for narrative content** (e.g. the "organizzazione del
   lavoro" chapter roadmap) — prose, not bullets. Itemize is still fine for
   genuinely enumerable technical content (e.g. "cosa è stato costruito": the
   channel, the four instructions, the grid, the eight kernels...).
5. **Don't invent new terminology that conflicts with terms already
   established elsewhere in the thesis.** He used "canali sincroni" / "registro
   di ready" for the corrected intro — that phrase was folded in only where he
   asked for it; the rest of the thesis already has an established term
   for the no-handshake case (`\emph{lockstep}`, capitolo 5 §5.4) — don't also
   call it "asincrono" elsewhere and end up with two names for the same thing.

**Scope:** applies to `docs`-facing thesis prose going forward
(implementazione.tex, risultati.tex, conclusioni.tex still need this pass —
they were written before this feedback arrived, not yet revised against it).
See [[project-neso-status]] for where the thesis stands overall.

---
<!-- feedback_state_what_a_metric_measures.md -->
---
name: feedback-state-what-a-metric-measures
description: "This user re-runs reported numbers himself — define exactly what a metric measures before reporting it"
metadata:
  node_type: memory
  type: feedback
  originSessionId: fc07fad4-9ccd-4a03-b134-7c659fe4b30f
  modified: 2026-07-30T10:55:22.900Z
---

The user independently checks quantitative claims against his own runs. On 2026-07-30 I reported Jacobi "convergenza esatta a k=19" for a 4×4 grid; he came back with "ma la convergenza la raggiungo a k=16 non 19". He was right and I had an off-by-one: my test reported the first *k* where `delta == 0`, i.e. the first iteration that changed *nothing* — one turn **after** the field reached its final value. Compiling with that number does one iteration too many, and the label said something the number did not mean.

**Why**: he is writing a thesis around these measurements, so a number that is off or mislabelled propagates into the written argument. He also reads the per-case output rows, not just the summary — the `k=16` he quoted was a different row of the same table, which is exactly why the ambiguity was hard to see.

**How to apply**: before reporting any measured quantity, (a) verify it by a second independent route, not just by re-reading the code that produced it — here, sweeping `ITER` until the field actually came out uniform exposed the off-by-one immediately; (b) state in the output *and* in prose exactly what the metric measures, and prefer the quantity the user will act on (the `ITER` to compile with) over the one that is easier to compute (the first no-op iteration); (c) when a metric can be read two ways, print the underlying series (e.g. the per-iteration `delta` trace) so any threshold can be read off, instead of one derived number. Applies to the comparative cycles/spin tables still to be built — see [[project-neso-restart-points]] point 5.

---
<!-- feedback_two_planes_rule.md -->
---
name: feedback-two-planes-rule
description: "the simulatore / array simulato distinction is the relatore's most repeated note — restate it in every chapter and section intro, keyed to the unit of measure"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: f82d218d-57c2-4adf-ab85-6966b6382a79
  modified: 2026-09-09T20:40:55.694Z
---

Danelutto's single most frequent marginal note on the NESO thesis is that a
sentence mixes the **simulatore** (the C program, its threads, its seconds)
with the **array simulato** (the modelled machine, its cells, its cycles).
Chapter 3's opening already defines the pair "una volta per tutte", but he
kept writing "non si capisce" on later intros that *use* the distinction
without re-anchoring it. Instances corrected 2026-09-09: the chapter 5
opening, the §5.5 opening (`il costo del array`, unqualified), the `memtest`
paragraph (`il difetto più insidioso di questa architettura` → `del
simulatore`).

**Why:** the reader who opens a results chapter directly has no way to tell
which of the two a number belongs to, and the words *modello*, *architettura*
and *piano* all drift between the two senses. Note that §3.5 uses *piano* for
the two **parallelism levels**, so reusing "i due piani" for the two objects
of measurement in chapter 5 collided with it — that collision was the actual
cause of one "non si capisce".

**How to apply:**

- Anchor the distinction to the **unit of measure**, which is checkable: a
  cycle count is a property of the array simulato and does not change with
  machine or thread count; a time in seconds is a property of the simulatore.
  State it that way rather than abstractly.
- Say which of the two each section measures, in the section's first
  sentence, and cross-reference where the other one lives.
- Never write *architettura*, *modello* or *piano* where *simulatore* or
  *array simulato* is meant. In this thesis *modello* is the **antonym** of
  *simulatore*, already used that way in three passages.
- When a behaviour is an artifact of the C implementation (contiguous
  `struct`s, a flat array, a `running` flag), say so explicitly and state
  what the real machine would do instead — he marks these every time.

See [[feedback-relatore-writing-style]] for the general register rule and
[[feedback-gloss-terms-at-first-use]] for the sibling terminology rule.

---
<!-- feedback_verify_reviewer_claims.md -->
---
name: feedback-verify-reviewer-claims
description: verify every claim in a review/feedback pass against actual code/data before applying it — reviewers (including other AI sessions) get facts wrong too
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 11c7e605-b1ca-45af-b81f-65d14eda906a
  modified: 2026-09-13T14:37:57.860Z
---

The user repeatedly pastes review passes on the thesis (from the relatore,
or from another verification session) and expects them checked against
`docs/dati/*.csv`, the `.s`/`.c` sources, or hand arithmetic before being
applied — not applied on the strength of sounding plausible. Confirmed
across several rounds in [[project-neso-status]]: a relatore's own draft
text misattributed a counting mechanism to the wrong assembly file
(`bordo.s` instead of `prodcons.s`/`chain.s` — `bordo.s` has no `s3`/`s4` at
all, verifiable by reading the eight-instruction file); an external
"verification" pass asserted a measured trend was reversing when the
underlying CSV showed monotonic decrease to the largest tested shape; a
math example meant to distinguish truncation from rounding used an input
where both operations give the same result; another pass claimed a data
point was missing from a plotted axis when it was present just unlabeled.

**Counter-example, 2026-09-13 — checking against the CSV is not enough when
the CSV and the claim share a source.** That same pass said the "2624 cicli"
of the equal-cycle control "couldn't be reconstructed", and I rejected it
because `controllo.csv` says 2624. The reviewer was right: `test_pesante.c`
checks liveness only every `BLOCCO` = 64 cycles, so every pesante CSV rounds
cycles up to a multiple of 64. The real counts (`build/neso`, which checks
every cycle) are 2609 (`PESO=1 ITER=62`) and 2573 (`PESO=32 ITER=8`). Verify
by an *independent* route — here, running the program under the simulator
directly — not by re-reading the file the disputed number came from.

Two more, 2026-09-09, both from the relatore's own spoken notes: he stated
that "il metodo vero divide per cinque", the four neighbours plus the cell's
own value — but Jacobi on the discrete Laplacian divides by four, because
four is the diagonal coefficient that the method moves to the left-hand
side, and "cinque punti" counts the stencil's points, not the divisor; the
`/4` kernel is the method, not an approximation of it (the only real
approximation is integer arithmetic instead of floating point). He also
marked "l'array sistolico nella sua forma classica" on `matmul` with "SOLO
PER MM, ce ne sono altri classici", which was right — and my proposed fix
cited Kung & Leiserson for schemes I had not opened the paper to check; that
citation had to be withdrawn, since their matmul is the hexagonal band-matrix
design where all three streams move, not this output-stationary one.

**Why**: applying an unverified correction can silently replace a true
statement with a false one, which is worse than leaving the original text
alone — and it happened in both directions (the relatore's notes and
independent review passes both contained errors that would have been
propagated if trusted at face value).

**How to apply**: before editing text in response to a review comment,
locate the concrete artifact the comment implies (a CSV row, a line in an
`.s`/`.c` file, a formula recomputed by hand) and check it. When the review
is right, say so briefly and fix it. When it's wrong, say so explicitly
with the counter-evidence, and don't apply that specific point — this
applies equally to a relatore's own draft phrasing and to a "verification"
session's findings; neither gets a pass without a check. Never assert what a
cited work contains unless the work is at hand — if there is no copy in the
repository, say so and drop the citation rather than reconstructing it from
memory. See
[[feedback_state_what_a_metric_measures]] for the sibling lesson about
metrics specifically.

---
<!-- project_accent_pass_gotcha.md -->
---
name: project-accent-pass-gotcha
description: "In NESO, blanket vowel+apostrophe -> accent replacements silently break C char literals and shell quotes"
metadata: 
  node_type: memory
  type: project
  originSessionId: e98b82df-3f1e-49fa-aa5b-8c37616c49b0
  modified: 2026-07-30T17:17:18.630Z
---

NESO mixes Italian prose and code in the same files, so a global `<vocale>'` ->
accento replacement corrupts real quote characters. Two confirmed breakages
(2026-07-30): `'E'` in the ELF magic check of src/elf.c became `'è`, and five
closing shell quotes in the Makefile `dati` target became `sommà`, `valorè`,
`totalè`, `deltà`, `spinte_rifiutatè`. The first broke the build, the second
broke only `make dati`, which `make test` does not cover.

**Why:** the apostrophe is a quote delimiter in C and sh, not only an accent
stand-in, and `\bE'` matches inside `'E'`.

**How to apply:** guard every rule with a `(?<!')` lookbehind, never add an
`E'` rule, and run `make test` plus a quote-balance check on the `dati` echo
lines afterwards. Related: [[feedback-state-what-a-metric-measures]].

Three more traps hit the same day, all in scripted passes over these files:

- `perl -CSD` without `use utf8` double-encodes every accent into mojibake
  (`così` -> `cosÃ¬`). Run these substitutions in **byte mode** instead: the
  UTF-8 bytes in the pattern and the replacement round-trip untouched.
- In byte mode a trailing `\b` after an accented character never matches, since
  the last byte is not a word character. Use `(?![a-zA-Z])` instead.
- `$\n` inside a Perl regex is read as the variable `$\` followed by `n`, so
  the pattern silently matches nothing. Anchor with `\n` alone, no `$` before
  it — and note this failed *identically* in the verification regex, which
  reported "0 residui" for a file that still had them. **Verify by looking at
  the bytes, not by re-running a variant of the same regex.**

---
<!-- project_neso_benchmark_method.md -->
---
name: project-neso-benchmark-method
description: "How to measure NESO timings so the numbers survive scrutiny: interleave conditions, run nothing concurrently, equalise run duration, take the min"
metadata: 
  node_type: memory
  type: project
  originSessionId: 01770a32-8b58-45ee-a59f-6daad15cb86a
  modified: 2026-09-13T14:38:01.043Z
---

Timing measurements on this machine (i7-12700H laptop, `powersave` governor, 6 P-core + 8 E-core, desktop apps running) produced **three different wrong answers** on 2026-08-16 before the method was fixed. Each failure mode is cheap to repeat and hard to spot after the fact.

1. **Blocked conditions read as an effect.** `make scala` originally looped `PESO` outermost, so each `PESO` block ran minutes apart. Machine drift between blocks showed up as a clean 65 % "effect of PESO" that did not exist. Fixed by interleaving: repetition outermost, `PESO` innermost, so drift hits every condition equally.
2. **Concurrent work contaminates the sweep.** Running `latexmk` while the sweep was going took load from 2.8 to 7.0 and repetition spread to 220 %. Do all writing and compiling *first*, then measure with nothing else running.
3. **Short runs are noise, not data.** Sub-second runs gave a non-monotone memory-wall curve with >100 % spread. Scaling `ITER` per grid so every run lasts 1–4 s dropped spread to 1–7 % and the curve became clean and monotone.

4. **A coarse sweep grid silently overstates precision.** `SCALA_FORME` jumped 32→48 with nothing between, and the thesis read that as "pareggio a 48×48". 48 was only the first shape *measured* above 1.0. Adding 40×40 and 56×56 on 2026-08-16 moved the answer to **40×40**. A claim can never be finer than the sweep that produced it: before quoting a threshold, check there is a measured point on each side of it.

Always take the **minimum** of repetitions, never the mean: interference can only add time.

**Speedup is a ratio, and the ratio moves opposite to the truth.** Reducing `MEM_SIZE` made the absolute time better and the speedup number *worse*, because it helps the serial baseline more. Never judge an optimisation by the speedup figure alone; report absolute time too.

**To separate compute intensity from cycle count**, hold the cycle count fixed and vary only the kernel: `PESO=1 ITER=62` and `PESO=32 ITER=8` both run 2624 *grid_step calls* (the harness checks termination every `BLOCCO`=64 steps), but the array's real cycle counts are 2609 and 2573 — every `cicli` in pesante/scala/memoria/controllo CSVs is rounded up to a multiple of 64 (up to 8.8 % inflation at 256×256 in `memoria.csv`). Their speedups match at 32×32 (0,78× vs 0,77×, re-measured 2026-08-16), which is what supports parallel efficiency depending only on $R \cdot C$. At 48×48 the minima differ by 15 % (1,91× vs 2,20×) and the five repetitions do **not** overlap, they only touch (1,77–1,91 vs 1,91–2,20, each rep over the 1-thread minimum): the point neither confirms nor excludes an effect — say exactly that. Comparing at fixed `ITER` instead shows a spurious trend that is just thread-creation cost amortised over more cycles.

**Hardware counters beat timings on this machine** (2026-09-13). 16-thread times are not reproducible (same run gave 14,3 and 4,2 ns), but miss *counts* are. `perf` needs `sudo sysctl kernel.perf_event_paranoid=1` (default 4; the user sets it himself and resets it). The i7-12700H is hybrid: sum `cpu_core/…/` and `cpu_atom/…/` events; pin 1-thread runs with `taskset -c 0`. Exclude initialisation (1 GB alloc + memset) by running two `ITER` values and dividing the count *difference* by the simulated-instruction difference. Measure in the same `PESO` as the table being explained: `memoria.csv` uses `MEM_PESO=32`, `scala.csv` all three. Findings: STLB = 2048 4K entries, hot state ≈ 490 B/cell; the 32→48 jump is TLB (0,07→1,00 misses/instr), L3 misses start at 192×192, and at 256×256 the parallel run misses more than the serial one — that is the speedup drop in fig. 5.7.

**How to apply**: before quoting any timing number, check which of these four traps it could be sitting in. See [[feedback-state-what-a-metric-measures]] — he re-runs reported numbers himself.

---
<!-- project_neso_restart_points.md -->
---
name: project-neso-restart-points
description: "Ranked NESO backlog agreed 2026-07-30 — what to do next and the verified facts behind each, none of it derivable from the repo"
metadata:
  node_type: memory
  type: project
  originSessionId: fc07fad4-9ccd-4a03-b134-7c659fe4b30f
  modified: 2026-07-31T09:31:13.453Z
---

Ranked restart points the user asked me to record when closing the session of 2026-07-30. State is in [[project-neso-status]]. My recommendation was **start with 2 (few lines, but it is a result), then 1 (it is the demo)** — the user had not yet picked when the session ended, so ask.

**1. DONE 2026-07-30 (same day).** `asm/matmul.s` (written by the user) + `tests/test_matmul.c` + `test-matmul` in the Makefile, output-stationary: cell (i,j) accumulates C[i][j] in `s1`, A flows W→E and B flows N→S, K via `--defsym`. `ARCH ?= rv32i` with `test-matmul: ARCH = rv32im` and `build/matmul.o: ARCH = rv32im` — the "assembler rejects what the interpreter lacks" argument survives per-kernel instead of being dropped globally. **Measured, exact on 28/28 runs (K ∈ {1,4,7,32} × 7 shapes): `cicli = 19 + 16(K−1) + 10(R+C−2)`** — the cell count does not appear, cost follows the diagonal R+C, not the work R·C. Also exact: `attese = 2.5·R·C·(R+C−2)`, **identical for every K**, i.e. spin is the one-off pipeline-fill cost (each cell waits 5·(i+j)) and does not grow with the work. `ritentativi = 0` everywhere. Efficiency on 144 cells: 7% at K=1, 33% at K=7, 70% at K=32 — the array pays off when K dominates R+C. The host feeder needs **no skew**: in lockstep a[i][k] must enter at cycle i+k, here the ready bit makes the operand pairing correct by construction — this is the matmul's real thesis argument, the mirror image of the NOBP measurement.

**Historical note on why it was ranked first:** Spec §6 is titled "Caso d'uso dimostrativo: moltiplicazione di matrici systolic — buon test/demo per la valutazione". `jacobi.s` is a stencil, not a matmul. Everything it needs is already unblocked: (a) a non-uniform, time-varying border feed — A's rows from west, B's columns from north, one per cycle — which is what `BORDO=n`/`grid_border_fill` cannot do and what per-cell `grid_push` was built for, so it would be that API's first real user; (b) `mul`, which needs `-march=rv32im` in ASFLAGS (**verified**: `rv32i` rejects `mul`, `rv32im` accepts it; the core already implements MUL/MULH/DIV/REM). No new C infrastructure. Roughly 40 lines of asm + a host feeder + one Makefile word. Caveat to raise when doing it: enabling `M` partly contradicts the doc's argument for keeping `rv32i` so the assembler rejects what the interpreter lacks — multiply *is* implemented so it is consistent, but change it knowingly.

**2. DONE 2026-07-30 (second session of the day).** The spec §8-D measurement now exists: `NOBP=1` in `core.c`'s `PCIO` case degrades the channel to a plain register (OUT overwrites, SETRDY always succeeds, ISRDY always 1, double buffer kept so only flow control differs), `make test-nobp` sweeps `RITARDO` on `prodcons.s`, `test_catena.c` measures instead of asserting when `attesa < 0`. **Measured** (QUANTI=5, expected 15): with backpressure the sum is 15 at every RITARDO; without it, 15 / 18 / 22 / 24 / 25 / 25 for RITARDO 0 / 1 / 4 / 8 / 25 / 60. Three results to keep: (a) lockstep is correct **only** at RITARDO=0, and by coincidence — producer and consumer are both 6 instructions per value; (b) one cycle of drift already loses data; (c) the saturation at 25 = QUANTI × last value means the consumer re-reads the last value five times, i.e. total loss, not degradation. Plus one not planned: **cycle counts are identical between the two modes for RITARDO≥1** (52/82/122/292/642), so the 97 rejected SETRDYs at RITARDO=60 cost zero cycles — backpressure is off the critical path whenever the consumer is the bottleneck. Combined with `chain.s` (quadratic spin) and `jacobi.s` (zero spin): **the ready bit is paid for only when the nodes are symmetric, i.e. exactly when it isn't needed.**

**3. DONE 2026-07-31, both halves.** `grid_init` guards `rows <= 0 || cols <= 0`; `load_elf` checks `fopen`/`fseek`/`ftell`/`malloc`/`fread`; `check_elf(content, size)` (signature gained `size`) rejects files shorter than `sizeof(Elf32_Ehdr)` **before** touching `content[0]`, and validates that the section table lies inside the file; `carica_elf_in_core(..., size)` validates each loaded section's `sh_offset + sh_size` against the file size, next to the pre-existing `sh_addr + sh_size > RAM_SIZE` check — one guards where the `memcpy` writes, the other where it reads. Verified: truncations at 2/40/51/52/200/1000 bytes all exit 255 with a message (40 bytes used to exit **0** with an out-of-bounds read); three hand-corrupted `.o` files (`sh_offset` past EOF, oversized `sh_size`, and a 32-bit-wrapping `sh_offset+sh_size`) all exit 1; intact files unchanged, `make test` green, all six CSVs byte-identical, valgrind clean.

**Two design points worth not re-deriving.** (a) The section-range guard lives in `carica_elf_in_core`, *not* in `check_elf`, even though `check_elf` already has `size` and it would have saved touching 8 call sites — putting it in `check_elf` would duplicate the `sh_type == 1 && (sh_flags & 2)` filter into a second function, which silently stops covering the `memcpy` the first time someone changes the filter. Same argument the user already wrote at `docs/project_parte_teorica.md:1125` about maps that duplicate a convention established elsewhere. (b) That filter must stay exactly `sh_type == 1 && (sh_flags & 2)`: `.bss` is `NOBITS`, occupies `sh_size` in RAM but **zero bytes in the file**, so a guard over *all* sections would wrongly reject any kernel that ever declares a real `.bss` (today every `.o` has one with `size=0`, so it would pass by luck).

**4. A debt the doc already flagged and that now bites.** `docs/project_parte_teorica.md` §7 says `print_state` shows only `data`, never `wp`/`rp`, so "un deadlock si distingue da una terminazione normale solo per congettura". With four-port kernels this now genuinely blocks `STEP` debugging. Printing `ch_isrdy` beside each channel is ~2 lines.

**5. Measurement is not testing.** The user framed remaining work as "fare tanti test"; worth pushing back once — tests prove correctness, but the thesis *argument* needs numbers, and today the only quantitative table is `chain.s`'s. One comparative table (kernel × shape → cycles, spin, hops, iterations) is the experimental section, and the headline is already measured: `chain.s` quadratic spin vs `jacobi.s` zero spin, i.e. **spin is the price of asymmetry, not of the protocol**.

**6. State the deadlock-freedom lemma once, as an architecture property.** Proved informally for `jacobi.s` in §12.3; it generalises: *any program that publishes at most one value per direction per round and completes all sends before any receive is deadlock-free on depth-1 channels.* Writing, not code.

**Explicitly NOT worth doing** (each analysed and rejected with a reason, don't reopen without new input): convergence criterion inside the array (needs reduce+broadcast nested per iteration, coordination dominates the 33-cycle computation and reintroduces positional branches); Game of Life (needs diagonal neighbours → two-hop routing); channel depth > 1 (the doc shows `wp`/`rp` already generalise); passive waiting instead of spin (motivation largely gone once Jacobi measured zero spin). Also: the per-cell `.data` question the user deferred to the relatore is probably made moot by matmul, whose data arrives from the border rather than `.data`.

---
<!-- project_neso_status.md -->
---
name: project-neso-status
description: "NESO project phase status (code complete; thesis writing started 2026-08-12) and the professor's binding rulings on the channel design"
metadata:
  node_type: memory
  type: project
  originSessionId: 09a3907f-9d9b-475e-ba9d-3ec560148da7
  modified: 2026-09-14T07:27:22.963Z
---

**Status as of 2026-07-30 (end of session): Fase 0-6 complete, every clause of `docs/spec_progetto_riscv_array.md` is now covered.** Seven kernels in `asm/` — `prodcons.s` (1×2), `chain.s` (1×C), `broadcast.s` (R×C), `memtest.s`, `bordo.s`, `reduce.s`, `jacobi.s` (Jacobi 5-point stencil) — each with an assert-based end-to-end test in `make test` that sweeps parameters. Suite green, build clean under `-Wall -Wextra`.

The narrative lives in `docs/project_parte_teorica.md` (~1630 lines, §1-12) — **read it instead of re-deriving**. It deliberately keeps earlier wrong predictions in place with a marked rectification rather than editing them away, so a passage and its correction sit together. What is *not* recorded in the repo is in [[project-neso-restart-points]].

**Why the two-phase channel design looks the way it does** (still binding): the professor was emailed about a scan-order-dependency bug found while writing `grid_step` (a same-tick SETRDY could become visible to a same-tick ISRDY depending on core iteration order, breaking reproducibility and the "1 cycle latency per hop" systolic fidelity). His reply confirmed: model it on Andolfo's thesis two-phase algorithm (current/next double buffering) but — new requirement — extend it to **both** the data value and the ready-bit control signals uniformly (the thesis only double-buffered values). Hence `Channel` = `data/wp/rp` + `data_next/wp_next/rp_next`, `ch_commit` copying all three at once, and `core.c` never touching `Channel` fields directly.

**How to apply**: don't second-guess this channel design as over-engineered — it is a direct implementation of the professor's explicit instruction, not a speculative abstraction.

**2026-08-05 — second ruling, supersedes part of the above: NO `pending` bit.** At the review meeting the professor approved everything except the channel: he wants a **level-transition system with mod-2 counters and comparators and no other internal mechanics**. The `pending` bit (which bridged the gap between `OUT` and `SETRDY`, two instructions in two cycles) was removed. Replacement, implemented and verified: `ch_write` is no longer gated on `ch_iswrt` (the `OUT` can never be refused — it only touches the private `data_next`), and `ch_commit` captures the data **only on the transition**, `if (wp_next != wp) data = data_next`. Zero new bits, zero new opcodes, and **no kernel changed** — every `.s` already looped `OUT/SETRDY/beqz` back to the `OUT`.

**Measured consequence, worth remembering**: removing `pending` made the array *faster*. It had been forcing a respin in the case "channel full at `OUT`, empty at the `SETRDY` one cycle later", which now succeeds first try. Only `catena.csv` moved (`prodcons` RITARDO=0: 49→45 cycles, 2→1 retries); the other five CSVs are byte-identical, because they never refuse a cell-to-cell `SETRDY`. `matmul`'s host-side `spinte_rifiutate` is unaffected since `grid_push` does write+setrdy in the same cycle.

**Agreed next steps — both DONE as of 2026-08-12**: (1) `#pragma omp` on `grid_step` (two regions, `schedule(static)`, no locks) plus the quiet-mode/`-O2` work that had to come first: killing the per-instruction `printf` and adding `-O2` gave 3.5× serial before any parallelism, and the parallel result is that below ~24×24 OpenMP *loses* (Jacobi 12×12: 0.035 s at 1 thread vs 0.27 s at 8 — measured in `docs/dati/tempi.csv`, the only file `make dati` does not regenerate; it comes from `make tempi`); (2) the SystemVerilog model lives in `hw/` (`neso_channel.sv`, `neso_if.sv`, two testbenches, verilator lint, yosys stat: 66 flip-flop + 6 gates per channel). Both are written up in `docs/teoria_risc.md` §10-11.

**2026-08-12 — thesis writing started.** `TemplateTesi/` (Pisa CS bachelor template) now holds the structure the relatore dictated: introduzione, strumenti, progetto_logico, implementazione, risultati, conclusioni — one `.tex` per chapter, named not numbered, with a first draft of prose drawn from `docs/riassunto_incontro.md` and `docs/teoria_risc.md`. Title: "Simulatore di un array sistolico bi-dimensionale basato su RISC-V"; relatore Marco Danelutto; the starting thesis is Stella Andolfo, "Simulatore Multi-Core RISC-V con Estensioni ISA per la Comunicazione tra Core" (bib key `TesiStella`). Architecture figures are `\fbox` placeholders, still to be drawn in TikZ. **Binding rule carried into the thesis: no number is typed by hand — every figure quoted comes from `docs/dati/*.csv`.**

**Also from the professor**: he wants **multiple** test kernels/applications including on a 2D grid, not a single matmul demo. Satisfied and then some — as of 2026-07-30 there are **eight** kernels, the eighth being the systolic `matmul.s` that spec §6 names as *the* demonstrative use case. All code items of the backlog are closed; what remains is the write-up. See [[project-neso-restart-points]].

**2026-08-16 — ninth kernel and the scalability result.** `asm/pesante.s` + `tests/test_pesante.c`: synthetic xorshift load, `PESO` rounds of mixing per iteration, pure RV32I, bit-exact C reference. Built to move the OpenMP break-even point; **it does not, and that is the result**. A cycle is by construction one instruction per cell, so `grid_step`'s per-cycle work depends only on $R \cdot C$ and the cycle count cancels in the speedup ratio. Break-even measured at **48×48**, reproduced across four sweeps. Method matters here: see [[project-neso-benchmark-method]].

Also settled: the commit phase was already an owner-partitioned map (`&risc[i].out_ch[d]`, `out_ch` is by-value in the struct), no change needed. Three constants that silently ignored `MEM_SIZE` now derive from it — `risc.c:90` used to zero 4096 words regardless and would corrupt the heap if the constant were lowered. `MEM_SIZE` got an `#ifndef` guard (default still 4096, not exposed in the Makefile, so §3.2 of the thesis stays true). Chapter 5 was reorganised: `chain`/`reduce`/`matmul` grouped under "Come i dati attraversano la griglia", four TikZ flow figures, six assembly snippets, and a pgfplots speedup chart whose coordinates are generated by `docs/dati/grafici.py` — never typed by hand.

**2026-08-17 — chapter 5 re-measured, chapters 4-6 style pass, pagination fixed.** Break-even is **40×40, not 48×48**: the old sweep jumped 32→48 with nothing between, so 48 was only the first shape measured above 1.0 (see [[project-neso-benchmark-method]] trap 4). `SCALA_FORME` now includes `40x40`/`56x56`, `SCALA_REP` is 9. `tempi.csv` had been generated with a restricted invocation (2 kernels, 2 thread counts); the full target gives 400 rows and falsified "frazione seriale sotto il 15 %" — it holds except on the two shortest runs (`chain` 1×5 at 23,7 %, `matmul` 12×12 at 19,1 %), where fixed harness cost dominates a run of tens of microseconds. Two new targets make previously hand-typed thesis numbers regenerable: **`make memoria`** → `memoria.csv` (locality cost at 1 thread, `ITER` paired per shape so each run lasts 1-2 s **up to 128×128 only** — 192² takes ~2,4 s and 256² ~3,9 s; the tab:memoria caption said "1-2 s" flat and was corrected 2026-08-17) and **`make controllo`** → `controllo.csv` (equal-cycle control; the target asserts all rows have identical `cicli` and fails otherwise). Two tables stay explicitly non-regenerable and now say so in their captions: 5.5 (compares three past simulator versions) and 5.6 (a prediction from a micro-benchmark absent from the repo).

**LaTeX gotcha that cost two rounds** (now also commented in `preambolo.tex`): the `listings` `float` key **has no effect in `\lstset`** — it must be in each `\begin{lstlisting}[...]`'s optional argument, or code blocks split across pages. Use `float=htbp`, not `tbp`: a float never splits regardless of specifier, and allowing `h` keeps it near its reference. Excluding `h` made listings defer to the next top-of-page slot, which landed *past the following subsection heading* — reduce's code appeared under the matmul title. Hence `\usepackage[section]{placeins}` plus a `\subsection` redefinition firing `\FloatBarrier`. Sentences must name floats by `\ref` rather than trailing a colon, or a float that moves strands its introduction.

**Partial drafts for the relatore: use the `\ifconclusioni` switch, not `\includeonly`.** `preambolo.tex` defines `\newif\ifconclusioni` (default true); `corpo.tex` guards `\include{conclusioni}` with it and `introduzione.tex` guards the §1.2 sentence that forward-references chapter 6. A draft file is then just `\input{preambolo}` + `\conclusionifalse` + `\input{corpo}` — see `bozza_capitoli_1-5_656276.tex`. **`\includeonly` is the wrong tool here**: for an excluded chapter LaTeX still reads its `.aux`, so the `\@writefile{toc}` lines put it in the index anyway and `\@setckpt` applies its end-of-chapter counters, which made the bibliography jump from page 47 to 52. Do not try to fix that by redefining `\include` — it is `\def\include#1 {` with a *space-delimited* parameter, so calling it as `\bozza@include{#1}%` swallows the following tokens and breaks every cross-reference in the document (44 `??`).

**2026-08-17 — chapter 5 pagination.** Six hand-placed `\clearpage` in `risultati.tex` were the main cause of the white bands and of one float-only page (Codice 5.4 + a third-of-a-page hole + Figura 5.4, zero prose): the one before `\subsection{jacobi}` stopped the text from flowing onto the page the two floats had taken. **Removing all six fixed most of it and shortened the chapter by a page** (thesis 56→55). Watch out when deleting a `\clearpage` that sits between two text lines with no blank line around it — it was acting as the paragraph break, and the two paragraphs silently merge. Residual, not fixed: the `\FloatBarrier` that `preambolo.tex` fires at every `\subsection` closes a page early whenever a subsection is too short to host its own float — §5.7.1 (two paragraphs + one table) still ends a third of a page early. Moving the table in the source and `[!htbp]` both changed nothing; the only fix is dropping that barrier, which is the one guarding against the reduce/matmul bug above, so it was left alone.

**2026-08-17 — chapters 1-5 cross-check and the intro's central claim.** The introduction used to state the thesis as *«il produttore alza il registro, il consumatore lo abbassa»* — word for word the single shared boolean that `progetto_logico.tex` §sec:canale refutes as insufficient. Rewritten on the professor's own skeleton, which is **binding**: *«impostare canali sincroni che usano ready bit in grado di modificare l'eventuale flusso di dati nel canale»* (the noun "flusso" was confirmed by the user, not recovered from the repo — the meeting notes don't record this sentence). The paragraph now names the registers explicitly, because they are real: `wp`/`rp` are flip-flops in `always_ff` in `hw/neso_channel.sv:66`, toggled `wp <= ~wp`, and two of the 66 counted flip-flops. What was wrong was never "registro" but "alza/abbassa" — two private registers each toggled by its owner, with readiness being the comparison `wp != rp`, not the value of either.

**Terminology, settled**: `ready bit` in all of chapters 1-5, nine occurrences, including the §5.4 section title. The professor writes "bit di ready" but that was a sentence-structure correction, not a terminology ruling. Uniform across all six chapters as of 2026-08-18 (the last "bit di *ready*" in `conclusioni.tex` went with the chapter-6 rewrite).

**2026-08-18 — chapter 6 rewritten, and one section deliberately frozen.** §6.1 now states **two** results side by side: the ready bit (correctness independent of relative speed, zero cycle cost, 66 FF + 6 gates) and the simulator's parallel efficiency depending only on $R \cdot C$ (break-even 40×40, `PESO`×32 does not move it, serial fraction 10,1 %→0,64 %, locality wall 15,9→83,4 ns). Every figure is copied from chapters 4-5, never re-derived. §6.3's first subsection must keep **exactly two** items in the order read-counter-then-parallel-region, because `risultati.tex:929` refers to "la seconda delle due modifiche indicate nelle conclusioni" in plain prose — deliberately not a `\ref`, since the `\conclusionifalse` draft would leave it undefined. **§6.2 "Considerazioni sull'esperienza di sviluppo" stays verbatim by the user's explicit instruction**: do not re-propose cutting it, even though its `pending` story has no antecedent anywhere in chapters 1-5 and its third paragraph ("su tutte le griglie di questo lavoro il parallelismo costa invece di rendere") now reads against §6.1.2.

**Same day, two revisions after review.** (1) §6.1.2 must **not** be framed as a disappointment — the user's objection was right and is worth keeping: independence from compute intensity *is* the desired result, and it is a deduction from "one cycle = one instruction per cell" that `pesante` then verifies, not a surprise. The word "negativo" is gone from ch. 6 (it survives in `risultati.tex:891`, which is the experimental sense and was left alone), the ratio $R \cdot C \times 35\,\mathrm{ns}$ over the two parallel regions is stated first, and the positive consequence is now explicit: above 40×40 threads help whatever the array runs, so the break-even need not be re-measured per program. (2) §6.1.1 was realigned to the rewritten introduction — "sbaglia in silenzio" → "un risultato numericamente definito ma errato, che nessun componente rileva" (the exact phrasing Danelutto corrected in ch. 1), the two-private-registers-and-their-comparison wording carried over, and the self-referential "giustifica la scelta di tenere il protocollo così povero" replaced by the decomposition **64 FF data + 2 FF counters**, i.e. synchronization state proper is two flip-flop per channel. (3) §6.3 "Memoria privata parametrica" **deleted at the user's request** — nothing `\ref`s it and the 15,5 GiB and the 5 % figures stay argued in §5.7.4; do not restore it. Chapter 6 ends at four future developments; thesis is 58 pages, compiles with zero undefined references.

**Three chapter-5 claims that did not survive re-derivation from the CSVs** (everything else did, including the matmul formula on all 21 rows, the 14 Jacobi convergence values, all eight `tab:memoria` figures and the control-sweep speedups): the `tab:memoria` duration window (above); §5.7.3's "nessuna curva sta sistematicamente sopra le altre" — `PESO`=32 beats `PESO`=1 on 9 of 12 shapes, so the claim now rests on the equal-cycle control sweep, which does hold exactly; and `tab:nobp`'s `attesa` column, which held the *expected* sum (constant 15) while "attese" is a metric defined in §5.1 — column dropped, value moved to the caption.

**Setup note**: `graphify` is wired into this project's local `CLAUDE.md`; query the graph per those rules, no need to re-explain it.

**2026-09-07 — chapters 4-5 revised through five rounds of Danelutto feedback plus internal verification; chapter 6 finalized; email to send ch. 4-5-6 drafted.** Danelutto sent two rounds of specific notes on `implementazione.tex`/`risultati.tex` (bold everywhere, "salto" used for both branch and inter-cell hop, unglossed terms, taratura→ needs an independent count, missing intros, a few factual asks). Applied and locked in as conventions: **`\textbf` eliminated from all six chapters** (replaced with `\emph{}`, plain text, or restructured sentences — none left, `grep -l textbf *.tex` on content chapters returns nothing); **"salto" reserved for branch instructions**, inter-cell hop is now **"attraversamento"** (one-cycle channel latency) vs **"passo di propagazione"** (the real per-hop cost, e.g. matmul's 10 cycles) — these are two different numbers and conflating them was the exact objection Danelutto raised twice; **"sweep" and "drenaggio" each get one footnote at first occurrence** (not inline parentheticals) giving the English term and the definition — user's explicit preference over glossing inline. §4.5 "La terminazione di una cella" (`sec:ecall`) is a new section, `ecall`'s effect and the memory-guard second termination path are both covered there. §5.2 renamed "Validazione dei contatori" — the independent count is `prodcons`/`chain`'s `s3`/`s4` (asm), not `bordo` (bordo.s counts nothing, 8 instructions, no s3/s4 — an error in Danelutto's own draft text that had to be caught, not applied).

**The crossover-point result was upgraded, not just re-explained — don't let a stale draft revert this.** Old story: predicted break-even 24×24 vs measured 40×40, blamed on *false sharing* + P/E core scheduling asymmetry. **Recomputed and found wrong**: the prediction used 35 ns/instruction measured on 64×64; the correct value on 12×12 is 22.5 ns (`0.0137 s / (144×4227 instructions)`). Rerun with the right constant, predicted and measured agree within 2% (0.179× vs 0.175×) — **there is no discrepancy left to explain**. `risultati.tex` §5.7.2/5.7.3 and `conclusioni.tex` §6.2 now say this: the analytical model is correct, the input constant wasn't. *False sharing* stays as a structural observation motivating the read-counter-relocation idea in §6.3.1 (not as the cause of the gap), and is itself refined: the four out-channels aren't universally in one cache line — cell stride is 16632 B (`≡56 mod 64`), so alignment rotates per cell with period 8 (offsets step by 8 B) and only **one cell in eight** has all four channels in a single line; the other seven straddle two (corrected 2026-09-13). The P/E-asymmetry paragraph that used to explain the gap was removed; core asymmetry now appears only as a reproducibility caveat (§5.1 "La macchina di sviluppo", §5.7.3), never as an explanation of the offset.

**Two rounds of *external* "verification" review (not the professor's) surfaced real bugs but also asserted false corrections** — every claim was checked against `docs/dati/*.csv` and the `.s`/`.c` sources before applying, and several were rejected: "matmul è l'unico programma con spinte dell'ospite rifiutate" was false (jacobi's `grid_border_fill` ignores rejections too; matmul is the only one that *counts* them) — fixed to say that. "Il controllo di terminazione diventa il termine dominante sulle griglie grandi" was unsupported — §5.7.3's own data shows frazione seriale monotonically decreasing to 0.64% at 256×256 (the largest shape tested), no rebound; termination cost is O(R·C), same order as the useful work, so it's a *constant floor*, not a growing term — fixed. The Jacobi point-fisso example (`B-1,B-1,B-1,B-1` truncated vs rounded) was mathematically wrong — both variants give 63 for that input; replaced with `64,64,64,63=255` (truncates to 63, rounds to 64) plus the hand-checkable 1×2 sequence. The "56×56 not on the x-axis" claim from one such review was itself wrong (the tick is there, unlabeled) — not applied. The same review's objection to "2624 cicli" was rejected against `controllo.csv`, **but the reviewer was right** (found 2026-09-13): the CSV is rounded by `test_pesante`'s `BLOCCO`=64, real cycles are 2609/2573. Lesson generalized into [[feedback-verify-reviewer-claims]].

**Style pass, all six chapters**: a "controllo formulazioni" pass (weak/templated constructions like "X: è Y", "ed è ciò che...", "è il caso in cui...", subjectless verbs with an ambiguous antecedent, positional table references like "le due colonne di sinistra") was run and the clear-cut fixes applied; a handful of borderline cases were deliberately left alone per user instruction ("lasciamo da parte gli ambigui") — don't re-flag `risultati.tex` around the `chi:iterazione-più-bassa` deadlock-proof sentence or the `pesante`/`È l'unico programma...` phrasing, they were reviewed and kept as-is.

**Thesis compiles at 70 pages**, zero `\textbf`, zero broken `\ref`, zero `Overfull`/`Underfull`, `make test` green.

**2026-09-08 — Danelutto replied to the ch. 4-5-6 email; a meeting is set for the afternoon of 2026-09-09.** Of the three open questions, points 2 and 3 came back fine ("va bene lstlisting con i rif ai numeri di riga"). Point 1 is a **new ruling on the channel**, and it is about *presentation*, not correctness — he opened with "sembra che tu abbia messo a posto diverse cosette" and never contested the design: the pieno/vuoto vocabulary is *eccessivo*, what matters is the state of the indicators, and "se la tua implementazione non usa il reset, non si capisce come funziona". His words locate the fault in *"per come è messa"* — the text, not the mechanism. He offered two acceptable forms: `IN` clears the indicator on read (**which is what the code already does**, `ch_read_c` guarded by `ch_isrdy`), or a separate `RESETRDY` instruction, which he calls "meglio". `RESETRDY` was costed and offered back to him: `funct3 = 4` is free, +1 instruction per receive, 8 of the 9 `.s` touched (`memtest` has no channel instructions), matmul body 16→18 and propagation step 10→12.

**He then sent a second round: handwritten annotations on chapters 3-6, ~50 notes, and he stopped at §5.5.1** ("arrivato fino a qui") — §5.6, §5.7 and the rest of ch. 5 are still unread.

**All of it — the channel walkthrough for the meeting and the full annotation analysis with verification verdicts and ready substitution text — is in `docs/canale_sintesi_confronto.md` (25 sections, two parts). Read that instead of re-deriving.** Its §16 lists the eight defects that were verified as real (notably: `conclusioni.tex:121` "due barriere invece di due per ciclo" compares a number with itself; "nessuna cella sa di stare sul bordo" is false, `reduce.s` branches on position and §5.3.6 says so; the single-`full`-bit argument is written backwards — in hardware the design is *unrealizable*, not merely order-dependent). §25 keeps the open checks, including a still-unverified off-by-one in Jacobi's 4227 cycles.

**Settled wording, do not re-flag (2026-09-14):** `progetto_logico.tex` §3.4 "nessuna cella ha la percezione di stare sul bordo" was corrected to that form in another session and the user wants it kept, even though `canale_sintesi_confronto.md` §16.2 suggested "nessuna cella *deve* sapere".

**Layout budget of chapter 5:** it ends on a full page, so any net line added in §5.5–5.7 spills 1–3 lines onto an orphan page before chapter 6 (93→94 pages). Check page count after every ch.5 edit; `\looseness=-1` did not help there.

**Two decisions that are his to make, do not guess them**: (1) `ospite` vs `host` — he wrote "io direi *programma ospite*" on ch. 3 p. 1 and then corrected `ospite`→`host` six times from p. 13 on; it is a global substitution either way; (2) whether the mark before his clock waveform in §4.7.1 is a "No" (a correction of "sul fronte" as the explanation) or just an illustration — needs looking at the paper.

---
<!-- project_thesis_layout_toolkit.md -->
---
name: project-thesis-layout-toolkit
description: "how to diagnose and fix page-break / half-empty-page problems in the NESO thesis — measure with ghostscript bbox on a scratch copy, and the listings inherit linespread 1.5"
metadata: 
  node_type: memory
  type: project
  originSessionId: f82d218d-57c2-4adf-ab85-6966b6382a79
  modified: 2026-09-13T14:13:46.090Z
---

Recurring task on `TemplateTesi/`: the user points at two page numbers and
asks to merge them, or at a page with three lines of text. Method that
works, established 2026-09-09:

- Printed page number + 4 = physical page number in the PDF.
- Measure, don't guess: `gs -q -dNOPAUSE -dBATCH -dFirstPage=N -dLastPage=N
  -sDEVICE=bbox file.pdf` gives the ink bounding box; a bottom coordinate
  well above ~90 means the page is half empty. Sweeping every page this way
  finds the bad pages the user hasn't noticed yet.
- Test variants in a scratch copy of the whole `TemplateTesi/` directory,
  never in place; compile twice, then check `exit`, `undefined`, `Overfull`
  and `pdfinfo | grep Pages` together.
- Two floats share a float page only if their heights plus `\@fpsep` fit in
  `\textheight` = 652.7 pt. Measure a float's real height with
  `\begin{lrbox}` + `\typeout{\the\ht\mybox \the\dp\mybox}`.
- **Root cause of most of it:** `\linespread{1.5}` applies to `lstlisting`
  too, so code is set at 18 pt baseline and a 17-line listing occupies 341 pt,
  over half a page. `basicstyle={\linespread{1}\footnotesize\selectfont
  \ttfamily}` in `preambolo.tex` sets listings solid, takes the thesis from
  76 to 72 pages and removes four half-empty pages at once — measured, clean
  build. Offered but not applied: it reflows every page number, so it is the
  user's call.
- Local alternative that was applied instead, for the matmul pair:
  `\scalebox{0.9}` on the tikzpicture plus one sentence cut from the
  caption. Scaling the drawing alone was not enough at any factor down to
  0.85 — the caption line is worth ~21 pt, more than 5 % of the drawing.

- Whole-thesis pass of 2026-09-12 (ch. 1-5): removed every manual
  `\clearpage`, `\pagebreak`, `\looseness` — they were tuned to older text
  and each new edit made them create the gaps they once fixed. Then fixed
  what remained structurally, 77 → 73 pages. What actually works here:
  - Most gaps come from `placeins` (`\FloatBarrier` before every section
    and subsection): a short subsection followed by a large float leaves the
    rest of the page blank. Fixes that worked: swap the order of two floats
    in the source (figure before listing), or place the figure's source
    just after the *next* section heading, so the heading fills the page
    and the figure lands at the top of the next page, right after its
    reference.
  - Every `lstlisting` must carry `float=htbp` (the preamble's stated
    policy); inline ones split across pages. When making one float, turn an
    introducing sentence ending in ":" into an explicit
    `codice~\ref{...}` reference.
  - `\looseness=-1` recovered nothing on these tightly-set paragraphs.
  - The matmul `\scalebox{0.9}` is still load-bearing: removing it pushes a
    half-empty page and a 3-line chapter end into ch. 5.
- **When a batch of wording edits breaks the layout, bisect it** (2026-09-13).
  Keep the pre-edit `.tex`, diff the two versions into hunks with
  `difflib.SequenceMatcher`, rebuild in a scratch copy with subsets of the
  hunks applied, and compare page count and gap list. One added line in the
  §5.1 machine paragraph (a TLB gloss) cost two pages and three gaps in ch. 5.
  Five added characters in §4.2, or a listing comment that wraps to a second
  line, cost one page in ch. 4. The fix is to keep the paragraph's line count:
  trim the same paragraph by as much as was added. Moving the gloss into the
  machine table instead made it 16 pt too wide.
- The ch. 5 table of the serial case (tab:seriale) sits after the 5.7.2
  heading on purpose. Moving it before the heading costs a page and two gaps.
- Render contact sheets (`pdftoppm -r 45` + PIL tiling, 6 per row) and
  *look*: the bbox number finds empty bottoms but not a colon split from
  its table or a listing cut in two.

**Why:** these fixes look like guesswork and aren't; the user asks "riesci a
unire" and expects a yes/no backed by a number, plus the cost of getting it.

See [[project-neso-status]] for where the thesis stands.
