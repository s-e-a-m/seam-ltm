# lessness — the hub's detector

`lessness` shows what travels on the SEAM hub.
It is a pass-through plugin: the audio goes through unchanged, and it has no automatable parameters.
Its design is in `docs/superpowers/specs/2026-11-25-seam-hub-design.md`, sections *The operator sees everything first* and *Design 3*.
The hub itself is described in `plugins/_common/hub/README.md`.

## What it shows

- **Members.**
  Who is registered on the hub, each with its state.
- **Proposals.**
  The proposals in the store, and for each one where every receiver stands: arrived, shown, applied, ignored.
  Under the pane, a note says that a receiver that changes stone no longer shows the decisions it took; the sheet keeps them.
- **The sheet.**
  The session sheet's path, whether it is being written, the lines held or dropped, and the last lines as the sheet carries them.
  A red mark in the footer, with the count of held lines, says the sheet cannot be written, for instance because its folder is on an unmounted volume.
- The window and the sheet use the same formatters, so they say the same things in the same words.

The operator sees everything first: before anything is decided, the whole state of the session is on one screen.

## What it changes

Two things, both on the sheet and nowhere else.

- **FOLDER** chooses the folder of the next sheet.
  The current sheet keeps its place until a new one is started.
- **NEW SHEET** closes the session sheet and starts a new one, in the folder FOLDER chose if one is waiting.

Both are GUI-thread actions on the view, with no bound parameter, as `SAVE` is in `strx` (see `doc/style/ui-style.md`).
It acts on no receiver and decides no proposal.

## Why it does not register

A plugin that registered on the hub would be a member of the session it shows.
It would open sessions, because a member arriving starts a sheet, and it would appear in them.
`lessness` only reads: it takes snapshots on the GUI thread and never holds a handle, so loading it or closing it leaves the sheet as it would have been.
The sheet is written whether or not `lessness` is loaded.

## The fold, seen from the operator's side

When the host stops and starts the transport, a member leaves and returns.
The hub's writer folds that into a single `resumes` line in the sheet, instead of a `leaves` and an `arrives`.
`lessness` shows the lines after the fold, so the members pane and the lines pane say `resumes`, never the `leaves` it replaced.

## The name

*Seam-less-ness*: the suite going on without a visible join, and a plugin whose job is to show every seam.
It is also Samuel Beckett's *Lessness*, a text made of sentences recombined, as a sheet is made of lines.
The name is Giuseppe's (2026-11-26).

## Source

- `source/lessness_view.h` — the lines each pane draws, SDK-free.
- `source/lessness_panes.h` — the panes.
- `source/lessness_buttons.h` — FOLDER and NEW SHEET.
- `source/lessness_processor.{h,cpp}` — the pass-through processor.
- `resource/lessness.uidesc` — the layout.
