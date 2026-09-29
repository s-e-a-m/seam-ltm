# SSCDO#2 porting report ("bibbietta") — design

Date: 2026-09-30.
Status: approved in conversation (approach C, five sections); this spec awaits Giuseppe's review.

## Purpose
A LaTeX report, in Italian, shared by Giuseppe Silvi, Davide Tedesco and Alice Cortegiani, that organises every reasoning of the SSCDO#2 surveys so that it can be read and judged during rehearsals.
The surveys fixed principles, processes and numbers; many choices now need a musical test.
The report gives each control its range, its consequences in numbers and in sound, and its state, and leaves room to record the rehearsal's verdict.
It is the bridge towards the documentation of the piece and the final operational score, which the CIM reviewers found missing.

Success: in a rehearsal, for any fader or cue, the three authors find in one place what it does, what range it moves in, what changes with it (durations, frequencies, levels), how it differs from Davide's original and why, whether it is settled or awaits their ears, and which audio file to compare with.

## Audience and language
Giuseppe, Davide, Alice.
Italian (CLAUDE.md allows Italian for study documents; the formal `doc/math/` stays English).
Written for readers who did not attend the sessions: every technical term explained once, where first used.
One sentence per line; affirmative explanatory voice (the suite's LaTeX style).

## Location and build
- `doc/study/sscdo2/report/sscdo2-porting.tex`, `Makefile` (latexmk, as `plugins/dslar/doc/study/`), `figures/` if needed.
- Class `report`, a4, 11 pt, three `\part`s; packages as the existing studies (babel italian, amsmath, booktabs, siunitx, hyperref, listings with the Faust style) plus xcolor and a margin-tag macro.
- The compiled PDF is committed, as the other study PDFs are, so that Davide and Alice read it without LaTeX.
- Build artefacts are gitignored.
- Working title: *Studio sul Corpo d'Ombra \#2 — il porting SEAM: principi, macchine, prove*.

## State markers
Three macros, each printing a coloured tag in the margin and an entry in a list of states:
- `\deciso` — measured and decided;
- `\daprovare` — decided on paper, awaiting the musical test;
- `\domanda` — a question for Davide.
A fourth macro, `\studio{<folder>}`, cites the study in `doc/study/sscdo2/` that proves a number.

## Part I — Principi e processi
Each principle once: statement, reason, where applied, one concrete number.
1. The port reasons, it does not copy: SEAM, then standard, then new code; the reference is the version played in Pd.
2. One block at a time; every check verified by mutation.
3. 96 kHz is the piece's rate: constants are anchored there (`sdt.delrmint`); the primes are a feature of the system and follow the rate.
4. Primes and incommensurability: the `sff.np` rule, the step-over, distinctness from geometry.
5. Process, not local remedies: the DC blockers.
6. Levels are set by playing: no compatibility make-up (LMO's +6 dB).
7. Precision: the VST computes in double; float concerns only the Faust IDE.
8. Lineage: in-phi-rev and *Canto alla durata*, DDELAY, SSCDO#4.

## Part II — Le macchine
One chapter per machine: the chain in a figure and a few lines, then one card per control, as a table of seven fields:

| Controllo | Ambito | Valore iniziale | Conseguenze in numeri | Rispetto all'originale | Stato | Studio / audio |

Cards known today:
- **LMO:** frequency (cues 97.44 Hz, glissando 97.44 → 112.67 Hz in 120 s; the 1.015 factor dropped); distance Δ between the two oscillators (0–50 Hz, beating from 10–15 Hz); volume CC81; the four channels (independent streams, +i Hz offsets); the band filter (B chosen, A/C/C3/C2 kept for Davide).
- **delRM:** distance in metres (7.291 m = 22 ms at start; the clarinet's partials per distance); volume CC82; channels 2 and 4 (about 15 dB of dynamic window, the ASP880's gain places the performance in it; transients to 0 dBFS at +6 dB); the integrator anchored at 96 kHz; the DC blockers (left out, Davide's listening decides).
- **stunedrev:** the four times (83/47/7/71 ms, range 1–100 ms, energy centroids 17 s to 3′20″, lengths to 99.9 % energy 44 s to 10′); input CC83 and output CC84 (the double feed, question for Davide); the 7 sections that change at the starting times; memory.
- **Choir:** a placeholder card (CC86), marked not yet studied.
- **Master:** CC88.
Each card's numbers come from the studies and cite them.

## Part III — Protocollo di prova
- Preparation: 96 kHz, inputs 5–8, outputs 9–12 to STONED, the ASP880.
- Listening cards in the order of the piece (cues and faders): what to listen for, at which value, which reference file, and blank fields *esito* and *decisione* to fill by hand.
- The open A/B sessions from the studies: LMO filter (A, B, C, C3, C2), LMO Δ, delRM distances, delRM DC blockers, stunedrev's four faces.
- The questions for Davide, numbered (double feed into stunedrev; cue outputs for LFU/RFD/RBU/LBD; shared noise seeds; the DC blockers' thinner low end; the LMO filter choice).
- A draft of the operational table: the same data reduced to one page.

## Sources and maintenance
- The report cites numbers and does not recompute them; every table names the study that produces it, so each number has one source.
- Automatic generation of the numbers from the studies is out of scope (YAGNI).
- After a rehearsal, a decision is written here first (the tag moves from `\daprovare` to `\deciso`), then in the session log and, where needed, in `seam.tedesco.lib`.
- Appendix: the index of the audio files, committed and regenerable (`render.py`).

## Out of scope
- The choir's study and the cue/MIDI layer in Reaper (placeholders only).
- The C++ plugins.
- The final public documentation of the piece (English) and the final operational score: this report prepares them.

## Checks before delivery
- `make` compiles without errors or undefined references.
- Every number in the report is found in the cited study's README, results or render log (a script greps a list of the report's key numbers against `doc/study/sscdo2/`).
- Every study folder in `doc/study/sscdo2/README.md` is cited at least once.
