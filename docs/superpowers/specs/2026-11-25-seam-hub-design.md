# The SEAM hub, the detector, and the first exchange (strx → BASTONE, BASTOOGE) — design

Date: 2026-11-25.
Who: Giuseppe Silvi (request, decisions); the agent (Claude: analysis, drafting).
S.T.OOGE is developed with Francesco Palmieri, guitarist.
Status: approved by Giuseppe, 2026-11-25.

## Intent

### Where this comes from

On 2026-11-21 the S.T.OOGE mk2g played its first concert, in Vienna, with Francesco Palmieri, in a new piece by Pierluigi Billone.
The chain was `lr2xhgr` → BASTOOGE → the Quadro 500 DSP amplifier, whose preset carried the calibration made at LEAP on 2026-11-19: five second-order sections, identical on the four faces (bells at 111, 406 and 1516 Hz, a high shelf at 65.5 Hz at −15 dB, a high shelf at 16 kHz at +18 dB).
With those filters the instrument sounded full and powerful; without them it was far too loud and out of control in the upper mids.
Giuseppe's notes from the concert ask for three things in BASTOOGE: an analyser of the four components before the decoding, equalisers on which to build the sound of a piece in a room, and the calibration moved from the amplifier into the decoder, fitted from `strx` measurements, as a small set of sections that could be carried into an analogue circuit ("à la Gerzon").
The analysis lives in `dream-stone` (private): `logs/2026-11-25-stooge-mk2g-concerto-vienna.md`.

Two milestones close the response study of the STONE family, and this work prepares for both:

1. **The anechoic room** (CNR), to be discussed at the doctoral board on 2026-12-02.
   There `strx` will need three inputs: stereo as today, tetrahedral (one tetrarec) and cubic (two tetrarecs, upright and inverted on z).
   Those inputs are the next design, not this one.
2. **A guitar glissando over the whole range**, played through the filters fitted in the anechoic room, for S.T.OOGE only.

The anechoic session should not invent the procedure.
The procedure is built and practised now, at LEAP, in the "wrong" room, so that the anechoic session only changes the data.

### Why a hub and not a link

The obvious build is one link: `strx` writes a table, BASTOOGE reads it.
Giuseppe asked instead for the suite to be thought of as a suite.
Every member says what it knows (a delay its time, a rotation its angle, `multipink` what it is playing, `strx` a band table, BASTOOGE the sections it applies), a hub gathers it, and the session file is the human copy of what passed among the members.

The reason runs deeper than tidiness.
Giuseppe works in a DAW because the hardware his practice needs does not exist yet: perhaps an FPGA, perhaps a mixer that takes for granted that every bus is a tetrahedral, three-dimensional signal.
The DAW is the bench where that machine is being worked out, what it must be and why.
The hub is therefore designed as the **control plane** of that future machine, beside the audio plane: few message types, typed and fixed in size, declared by each member.
The catalogue of messages is itself part of the answer to "what must the machine carry".

### The operator sees everything first

In this first stage no member acts on what it hears by itself.
The hub carries information; every effect waits for a gesture of the operator.
The procedures are ones a person has to understand, and therefore accept, because they are learning to work with instruments that exist nowhere else.
As in the first days at a particle accelerator, first you want to see the particles.
What can be automated will be said by practice, once looking is no longer experience because the result has already been lived.
The design must let that rule change later without changing the design.

## Goals

1. **The hub**: `calbus` generalised into the suite's message hub, in-process, one shared dylib, plain C ABI.
2. **The sheet**: one session journal, append-only, written by the hub alone, human-readable and machine-readable, always written.
3. **The detector**: a pass-through plugin that touches no sound and shows what passes on the hub.
4. **The migration** of `multipink`, `ltglide` and `strx` from `calbus` v1 to the hub, after which `calbus` v1 is removed.
5. **The first exchange**: `strx` publishes band tables; the decoder fits second-order sections from them and applies them after the operator accepts.
6. **A shared calibration engine** in `plugins/_common/`: three filter blocks (difference, common, piece/room), a high-pass for a subwoofer, an analyser of the four components, the fit, and the hub client of a decoder.
7. **BASTONE**, a new plugin: the generic decoder of the STONE family, built on that engine, with a body model per instrument.
8. **BASTOOGE, second edition**: the same engine, for S.T.OOGE mk2g only.

## Non-goals

- Communication across processes or machines: no Pd, Max or Csound members, no network.
  The hub lives in one DAW session (Reaper, Nuendo).
- Any automatic action: every received message waits for the operator.
- New announcements from plugins that do not need them today (delay times, rotation angles).
  The suite's rule holds: no peer awareness retrofitted speculatively (`CLAUDE.md`, *Peer-aware plugins*).
  The catalogue is designed so they can be added; they are not added.
- The tetrahedral and cubic inputs of `strx`: next design.
- Finalising BASTOOGE for the guitar.
  BASTOOGE is also Francesco Palmieri's plugin, and it will become specific to the guitar (the second milestone, the glissando, prepares that); this design keeps it a decoder with the shared engine and adds nothing guitar-specific.
- Constraining the fitted sections to the Quadro 500 (eight slots, fixed types).
  The amplifiers become amplifiers only; their limits do not enter the plugins.

## Anti-goals

The work fails, even if it runs, if:

- a member changes its sound because of something it heard on the hub without the operator's gesture;
- the sheet misses an exchange, or records one that did not happen, or two members' lines interleave mid-line;
- a plugin's sound depends on the hub's dylib being present (the bus is an observer, as `calbus` already states: a missing dylib means no exchange, never no sound);
- anything on the audio thread locks, allocates or touches the disk;
- the fitted calibration cannot be read as a list of second-order sections (type, frequency, Q, gain), which is what makes it transposable to analogue.

## Constraints

- VST3, hand-written C++, VSTGUI, the suite's window standard (`doc/style/ui-style.md`) and `_template.uidesc`.
- Faust is the specification for DSP (`seam.st.lib` for the decoders); the fitting procedure is not DSP and has a Python reference instead (see below).
- The hard parts already solved in `calbus` carry over unchanged in spirit: one dylib loaded by every member (static state does not cross `.vst3` bundles), plain C ABI with fixed-width types, an opaque handle with a registration epoch, a per-slot seqlock for wait-free writes from the audio thread, a layout pinned by `static_assert` and versioned.
- State blobs follow the suite's append-only contract (`seam_state.h`): BASTOOGE's existing three doubles stay first.
- BASTOOGE is in Palmieri's hands, in his own sessions, where there may be no hub, no `strx`, no detector: it must work fully without them, and the calibration of his unit travels to him as a BASTOOGE preset.
- Sentence-per-line for every document.

## Approaches considered

1. **Generalise `calbus` into the hub.** Same dylib and C ABI, extended with a member registry, a catalogue of message types, a lifecycle for each message, and the single writer of the sheet. `multipink`, `ltglide` and `strx` migrate; `calbus` v1 stays until they have.
   Reuses the solved parts; touches three working plugins.
2. **A new hub beside `calbus`.** Nothing working is touched, but the suite gets two buses (the ad hoc mechanism Giuseppe wants to avoid), and the sheet would not see `multipink`.
3. **The sheet as the hub.** Every plugin writes and reads a shared file. Concurrent writes, polling the disk, no audio-thread guarantee: every reason `calbus` became a dylib applies.

**Chosen: 1**, by Giuseppe.
The migration of three plugins is small and is also the proof that the catalogue holds.

## Design

### 1. The hub (`plugins/_common/hub/`, `libseamhub.dylib`)

**Members.**
A member registers at `setActive(true)` and receives an opaque handle (as in `calbus`).
At registration it declares: its plugin name (`strx`, `bastone`, `bastooge`, …), its instance number (assigned by the hub, `strx#1`, `strx#2`), its `stoneId` (1..8, 0 undeclared), and the message types it **says** and **hears**.
The declaration is what the detector shows and what the sheet records when a member arrives or leaves.

**Two kinds of message.**
`calbus` already found that announcements have two temporal natures (its comment on `SeamCalbusRecord`: pink is state, glide is event); the hub keeps both, explicitly.

- **State**: what is true now about a member (what `multipink` plays, which test mode a decoder is in, the cut).
  One slot per member per type, overwritten, wait-free from the audio thread, read by snapshot (the `calbus` seqlock, unchanged).
  The sheet records a state only when it changes, at a bounded rate.
- **Proposal**: a self-contained object offered to whoever hears its type (a band table, a set of sections).
  Each has a hub-assigned id, an author, a time, the `stoneId`, and a **lifecycle**: `arrived` → `shown` → `applied` or `ignored`.
  The lifecycle is kept **per proposal and per receiver**: two decoder instances hearing the same table each have their own state for it, and the sheet names the receiver in every transition.
  The detector only observes: its view does not count as `shown`.
  `applied` and `ignored` are not final: an operator can apply a proposal again (to another block, or after reverting), and each application is a new line in the sheet.
  Proposals are posted from a GUI thread, never from `process()`; they are kept in a bounded store (the last N per type, N decided by the builder, at least 16).
  The receiver moves a proposal through its lifecycle with one call per transition, and every transition is a line in the sheet, with who made it (`operator` for a gesture).

The lifecycle belongs to the hub, not to each plugin: the day practice says a type may apply itself, one rule changes (a receiver may move `arrived` straight to `applied` for that type), and the design does not.

**The catalogue.**
Each message type is an enum value with a fixed-size POD payload, pinned by `static_assert` like `SeamCalbusRecord`, and versioned with the hub.
The types of this design:

| type | kind | said by | heard by | payload |
|---|---|---|---|---|
| `pink` | state | `multipink` | `strx`, detector | as `calbus` v1 `pink` |
| `glide` | state (with pass counter) | `ltglide` | `strx`, detector | as `calbus` v1 `glide` |
| `decoder` | state | `bastone`, `bastooge` | `strx`, detector | test mode, bypass, cut, ids of the applied calibrations, whether the piece/room block and the sub high-pass are on |
| `band-table` | proposal | `strx` | `bastone`, `bastooge`, detector | the 31 ISO 266 bands: centre, `comp_db`, `level_db`, `settled`; sample rate, integration time, bands measurable, source as text, the source level from the `pink` state, and the `decoder` state seen at save time |
| `sections` | proposal | `bastone`, `bastooge` | detector | the fitted sections of one block: block (difference or common), count, per section type/f/Q/gain, the residual RMS and maximum, the ids of the tables it was fitted from |

A `band-table` carries the `decoder` state it was measured under, read from the hub when `strx` saves.
That is how the hub pays for itself on the first day: today the operator writes "W" or "X" in the `note` field by hand; with the hub the table knows which mode the decoder was playing.

Maximum payload is fixed by the builder to fit the largest type with room for the next design (the tetrahedral and cubic tables will be bigger); the record layout is the ABI, as in `calbus`.

**Failure.**
If the dylib is missing or its version does not match, the client runs in null mode, as `calbus_client` does today: every member still makes its sound; the exchange simply does not happen, and each member says so in its own status line.

### 2. The sheet

One file per session, written by the hub alone, on a background thread inside the dylib.
Members never open it; they post to the hub, and the hub appends in order of arrival.
Lines are posted to the writer through a lock-free queue; nothing on the audio thread waits for the disk.

- **Append-only**, never rewritten.
- **One event per line**, so git diffs are clean.
  Tables and sections are written in full, one row per line, indented under their event line, so the sheet stands on its own.
- **Readable by a person and by a program**: a fixed time column, the member, the verb, the type, then the content.
  Indicatively:

  ```
  14:32:07.412  strx#1      says    band-table #12  stone1  decoder=W cut=160  31 bands  646.6 s
                  20      19.95   +3.0   -41.2   1.00
                  ...
  14:33:40.020  bastooge#1  shows   band-table #12
  14:33:52.881  operator    applies band-table #12 on bastooge#1 (block common)
  14:33:52.903  bastooge#1  says    sections #13  common  6 sections  residual 0.8 dB rms, 1.9 max
                  peak   111.4  2.00   -6.0
                  ...
  ```

  The exact grammar is the builder's, written down in the hub's README with a small parser in `tools/` that reads it back (the parser is the proof that the sheet is machine-readable).
- **Always written**, detector or not: the testimony of a session must not depend on having opened a window.
- **Where**: a folder chosen once, kept in a suite preference file (`~/Library/Application Support/SEAM/hub.conf`), changeable from the detector.
  VST3 does not tell a plugin where the DAW project lives, so the folder cannot follow the project.
  If the folder is unreachable (unmounted volume), the hub keeps the lines in memory up to a bound and the detector says so in red; it never blocks.
- **A session** starts when the first member registers and ends when the last one leaves; the detector can close the current sheet and start a new one.
  The file name carries the start time.

### 3. The detector

A plugin that passes audio through untouched (as `strx`), with no automatable parameters.
It can sit anywhere; the master is the natural place.
Its window shows:

- the members present, with what each says and hears and its `stoneId`;
- the live states;
- the proposals with their lifecycle, the pending ones first;
- the sheet: its path, whether it is being written, the last lines.

It changes nothing but the sheet's folder and "start a new sheet".
It does not apply proposals on behalf of receivers: the gesture happens in the receiver, where its effect is heard and seen.
Its name is Giuseppe's to choose; this document calls it the detector.

### 4. The migration

`multipink` and `ltglide` move their records to the `pink` and `glide` state types; `strx` reads them from the hub with the same digest logic (`strx_calbus_digest.h` becomes a digest of hub states).
Behaviour seen by the operator does not change, except that the sheet now records it.
`calbus` v1 is removed once the three have migrated and their tests pass on the hub.

### 5. The first exchange: strx → the decoder

What this section says of "the decoder" holds for BASTONE and BASTOOGE alike: it is the shared engine (section 6).

**strx** gains one gesture: "propose" beside "save".
It posts the current band table as a `band-table` proposal (and still writes the text file, unchanged, which stays the operator's record).

**The decoder** gains a `stoneId` setting (0..8, saved in the state, 0 undeclared), so it can be told which physical unit it decodes.
A proposal reaches a receiver when their `stoneId`s match, or when either is undeclared; in that case the receiver shows the proposal with a warning that the unit is not declared.
A pending proposal appears in its window with its mode, time and integration (`band-table #12 · W · 14:32 · 646 s`), and two actions: **apply to …** (choose the block) and **ignore**.
Applying fits new sections, off the audio thread, and swaps them in on the next block, crossfaded so nothing clicks.

**What is measured, and which block it feeds.**
The target is the **power** of the instrument, the sphere of sound, not the frontal response (Giuseppe's decision).
The measurements of 2026-11-18 support the reading below: the decorrelated case equals the power mean of the four modes at the microphone within ±0.6 dB (BASTOOGE design, *Why a decoder*).

All tables are measured **with the decoder in the chain, bypass off and the current calibration in place**, so that every table is a residual (see *Iteration*).

- **Common block** (after the matrix, identical on the four faces): fitted from a table measured in **Normal with decorrelated sources** (the D case of the measurement protocol), the stereo estimate of the power available today.
  The target is **flat**, "linear by thirds of an octave within the limits of the single instrument" (Giuseppe): the fit runs only between a lower and an upper limit, set by the operator in the decoder and saved with it (defaults 63 Hz and 16 kHz); outside them the bands weigh zero, so the fit never tries to lift what the instrument cannot give (an 8" reflex cone at 30 Hz).
  The target is `comp_db` itself, which is already normalised to the table's own mid-band mean.
  It carries what the four modes share and what is specific to the unit: the cone roll-off, narrow resonances of the body (2.5 and 6.3 kHz on S.T.OOGE), the reflex hump.
  It is what migrates out of the amplifier.
- **Difference block** (before the matrix, on W only, in series after the model bell and all-pass): fitted from a pair of tables, one in **Test W** and one in **D**, taken at the same position and the same source level.
  `comp_db` cannot give the W/dipole ratio, because `strx` normalises each table to its own mid-band mean (`strx_bands.h`, `compensation()`); the ratio comes from `level_db`.
  The dipole power follows from the two tables without rotating the body: P_D = (P_W + 3·P_dip)/4, so P_dip = (4·P_D − P_W)/3, the relation already used on 2026-11-18 (`dream-stone`, `logs/2026-11-18-stooge-mk2g-primo-ruggito.md`).
  The target is the balance the model intends, W at the level of the dipoles where the dipole high-pass lets them through: 0 dB of measured W/dipole ratio, weighted by the high-pass's power response, so bands below the cut weigh zero and the transition weighs partly.
  The fit refuses a pair whose source levels differ (the level is in the `pink` state read at save time and carried in the table) and asks the operator to confirm the position is the same, since the hub does not know where the microphones are.
  The operator chooses the pair explicitly in the decoder among the received tables; the default is the latest W and the latest D for that body.
  It corrects what the model `r(f)` does not follow for this unit.
  The decoder keeps the tables a block was fitted from in its state, so that when the operator moves the cut the difference block is marked stale and can be refitted at once, without remeasuring.
  For transposition, the whole W branch is read as one list: bell, all-pass, then the difference sections.

The dipole branch keeps only the LR4 high-pass: by the tetrahedral symmetry one filter serves the three dipoles, and their correction is what the common block and the difference block already express between them.
Anything the two blocks have in common belongs to the common block; the difference block carries only the difference.

**Iteration.**
A new measurement is taken with the current calibration in place, so `comp_db` is the residual.
The fit does not stack sections: the new target is the current block's response times the residual, and the block is refitted whole.
The block stays small at every round, and the sections in it are always the whole calibration, which is what makes them transposable.

**The fit.**
Second-order sections only, minimum-phase: peaking, low shelf, high shelf.
Least squares in dB on a log grid, weighted by band (bands with `settled` below 1, bands outside the measurable range and bands below the dipole cut for the difference block weigh zero).
The count is the smallest that brings the residual under a threshold, up to a maximum (proposed: 8 for the common block, 4 for the difference block; Q from 0.3 to 10; gain within ±18 dB).
Room modes are the known trap of a correction from one position: below a room limit sections narrower than a third of an octave are not allowed, so the fit cannot dig a hole that exists at one point of the room.
The room limit is an operator setting in the decoder, about the room's Schroeder frequency (default 200 Hz, as at LEAP), and 0 in the anechoic room, where there are no modes to fear; it is recorded with every fit.
**Headroom**: the common block is normalised so its highest gain is 0 dB, and the offset is shown and recorded; the 2026-11-19 preset (a +18 dB shelf at 16 kHz on top of a −15 dB shelf) is exactly the case.

The fit has a **reference in Python** in `tools/` and the C++ in the plugin; both run on the same tables and must agree within a tolerance the builder fixes, as the C++ DSP agrees with Faust today.

### 6. The shared engine, BASTONE and BASTOOGE

Two plugins, one engine.
Giuseppe wants them separate: BASTOOGE is used by Francesco Palmieri too, and will be finalised for the guitar; BASTONE is the generic decoder of the family.
Today they may look alike; they are expected to diverge.

**The engine** lives in `plugins/_common/` beside `seam_st.h`: the filter blocks, the sub high-pass, the four-component analyser, the fit, the decoder's side of the hub (hearing tables, the pending list, the gesture, posting `sections` and `decoder`).
Each plugin is a thin shell on it, with its own window, ids and state.

**BASTONE** carries a **body model** selector.
Its first entry is S.T.ONE, from the 2026-11-19 measurement by modes (f1 = 344 Hz, `dream-stone`, `logs/2026-11-19-stone-modi-bastone.md`): its bell table is generated the same way as the mk2g one (`stone.bastooge` in `dream-stone`, run at f1 = 344 Hz) and isolated in `seam_st.h`, which already keeps the constants in one table "so that a second body can have its own".
The mk2g model is a second entry, so S.T.OOGE can be measured through BASTONE too.
STONED and S.T.ON3L enter when they have been measured the same way.

**BASTOOGE** keeps the mk2g model only, and its first edition's parameters and behaviour.

The signal flow of both becomes:

```
AmbiX ─┬─ W ─────── bell(fc) → all-pass(fc) → [difference block] ─┐
       └─ X, Y, Z ─ high-pass LR4(fc) ─────────────────────────────┴─ matrix ─ [common block] ─ [piece/room] ─ [sub high-pass] ─ 4 faces
                                                                              ▲ calibration     ▲ choice, by hand
```

- **Difference** and **common**: calibration, fitted from `strx`, applied by gesture, shown as their list of sections (type, f, Q, gain), readable as an analogue topology.
- **Piece/room**: a parametric of 8 sections, by hand, after the calibration and separate from it in the window, so that a choice of timbre never touches the calibration.
  Its sections are VST3 parameters (type, frequency, Q, gain, on/off), automatable, because the sound of a piece may change within the piece; calibration sections are state only, never automatable.
- **Sub high-pass**: LR4, off by default, frequency from 40 to 200 Hz.
  At Vienna the amplifier high-pass went to 142 Hz for a subwoofer, just under the dipole cut; in the decoder it is a Linkwitz–Riley that a subwoofer's low-pass can match.
- **Analyser of the four components**: third-octave levels of W, X, Y, Z at the input, before the filters, on the same ISO 266 bands as `strx`, so a reading here and a reading there name the same bands.
  A switch shows them after the pre-matrix filters instead, which is what the dipoles actually receive.
- **Bypass** keeps its meaning (filters out, matrix in), and now removes the calibration and piece/room blocks too.
- **State** (BASTOOGE): the existing three doubles first, then the new fields appended (`stoneId`, fit limits, room limit, sub high-pass, the applied calibration sections and the tables they came from); the applied sections are part of the state, so the host preset recalls them.
  BASTONE, being new, has no legacy: its state carries the body model and the same fields.
  The calibration is also recorded in the sheet at every application, so it can be reconstructed without the preset.

The Faust specification in `seam.st.lib` gains the three blocks as cascades of second-order sections and the sub high-pass; the C++ is written from it, and the A/B harness (`tools/faust-bastooge-ab.sh`) extends to them.

## Order of work

Giuseppe's order: the `strx` → decoder exchange first, on today's stereo input, so it can be tried at LEAP at once; then the tetrahedral and cubic inputs of `strx` (next design).
S.T.OOGE mk2g is with Palmieri, so at LEAP the instrument at hand is S.T.ONE: the exchange is first proved through **BASTONE** on S.T.ONE, and BASTOOGE follows on the same engine.
Within this design:

1. The hub and the sheet, with their tests, and the migration of `multipink`, `ltglide`, `strx` (the migration is the first proof of the catalogue).
2. The detector.
3. The engine's blocks, the sub high-pass and the analyser, with manual sections first (so the DSP is verified before the fit exists); BASTONE as its first shell, with the S.T.ONE model.
4. The fit (Python reference, then C++), and the exchange: "propose" in `strx`, the pending proposal and the gesture in BASTONE.
5. A session at LEAP on S.T.ONE: D and W measured, proposed, applied, remeasured; the sheet of that session goes into `dream-stone` beside the measurements.
6. BASTOOGE, second edition, on the engine; a preset with the calibration of Palmieri's unit when it can be measured again.

## Verification

- Hub unit tests in the style of `tests/seam_calbus_test.cpp`: registration churn, stale handles, torn reads, the store bound, lifecycle transitions, layouts pinned.
- The sheet: a test that posts from several threads at once and parses the file back with the parser in `tools/`, checking order, completeness and that no line is broken.
- Null mode: every member built against a missing dylib still produces the same samples.
- The engine: the Faust/C++ A/B for the new blocks.
- BASTOOGE: with the three new blocks empty and the sub high-pass off, the output equals the first edition sample for sample.
- BASTONE: with the mk2g model selected and the blocks empty, the output equals BASTOOGE sample for sample.
- The fit: Python and C++ on the tables of 2026-11-18 and 2026-11-19 (in `dream-stone`, copied into the test data as the BASTOOGE design already did for its ratio table), agreeing within tolerance; and on synthetic tables made from known sections, recovering them.

## Calls made in drafting, for Giuseppe to check

After a builder's reading, the agent settled these; each can be overturned.

- The difference block from **W and D**, not W and X: the dipole power follows from D without rotating the body.
- The lifecycle **per receiver**; the detector does not mark `shown`; a proposal can be applied again.
- The difference block **in series after the bell**, refitted from stored tables when the cut moves.
- **Flat target** between operator limits (defaults 63 Hz – 16 kHz); room limit 200 Hz by default, 0 in the anechoic room.
- The piece/room sections as **automatable parameters**; calibration as state only.
- A **`stoneId` setting** in the decoder; undeclared matches anything, with a warning.
- The exchange proved first on **S.T.ONE through BASTONE**, since S.T.OOGE is with Palmieri; BASTONE carries the mk2g model too.

## Left to the builder

- The exact ABI layout, the payload maximum, the size of the proposal store.
- The grammar of the sheet (within the shape above) and its parser.
- The crossfade when sections are swapped.
- The fitting algorithm within the constraints above (initialisation, optimiser, stopping rule), and the Python/C++ tolerance.
- The window layouts of the detector, BASTONE and the new BASTOOGE zones, within `ui-style.md`.
- Whether the decoders' windows grow to the L format.
