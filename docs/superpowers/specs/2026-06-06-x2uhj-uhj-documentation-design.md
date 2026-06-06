# X2UHJ — UHJ Mathematics Documentation (Design Spec)

Date: 2026-06-06 (revised after the realization research spike)
Status: Approved (brainstorming); revised by spike finding
Owner: Giuseppe Silvi (grammaton)

## Purpose

Produce a standalone LaTeX document that explains the mathematics of the `x2uhj` plugin step by step.
The document serves SEAM students as a *Learning Through Making* artifact.
It also serves as the complete mathematical deposit from which a later integrative paper extracts.

The document carries two derivations.
The first derivation covers Gerzon's UHJ C-format matrix coefficients, verified numerically by inverse verification.
The second derivation covers the sample-rate-independent design of the quadrature all-pass network, the original contribution of this work.

## Spike finding (central to the second derivation)

A research spike established the precise meaning of "sample-rate independent" for this network.
Sample-rate independence is a property of the **design procedure**, not of a fixed coefficient set.
A fixed all-pass (f, Q) set realized through the RBJ bilinear form holds the 90° quadrature only near the sample rate it was designed for; the bilinear frequency warping shifts the broadband phase with fs.
Measured quadrature error (max over 20–20000 Hz): the shipped digital design reads 1.36° at 48 kHz, 22° at 44.1 kHz, 47° at 96 kHz, 54° at 192 kHz; the pure s-domain analog design reaches 0.40° in the continuous domain yet 69° at 48 kHz once realized.
Re-running the digital phase fit at the actual sample rate restores the quadrature at every rate (2.04° at 44.1 kHz down to 0.42° at 192 kHz).
The deliverable therefore stores a **precomputed per-rate (f, Q) table** for the standard sample rates (44.1/48/88.2/96/176.4/192 kHz), each entry designed offline by the same fs-free procedure.
The plugin C++ change that consumes this table is a separate follow-up with its own spec.

## Scope

### In scope

- A LaTeX PDF in `plugins/x2uhj/doc/`, written in English.
- Writing follows two house rules: one sentence per line, and affirmative explanatory voice.
- Coverage spans the full UHJ C-format: L, R, T, Q.
- New design-time Python tools under `plugins/x2uhj/tools/`.
- A precomputed per-rate (f, Q) coefficient table (`coeffs_perfs.json`) for the standard sample rates.
- Regenerable figures under `plugins/x2uhj/doc/figures/`.
- A migration-path appendix that records how the plugin consumes the per-rate table, and points to the separate plugin-fix follow-up.

### Out of scope

- The peer-review paper itself, which becomes a later extract with its own spec.
- The plugin C++ fix that selects coefficients by sample rate; this is a separate follow-up with its own spec and audio testing.
- Changes to `coeffs.json` and the shipped `x2uhj_coeffs.h`; they stay intact in this work.
- Changes to the plugin GUI.

## Document structure (pipeline-driven, with historical intro)

1. **Introduction and historical framing.**
   Describe UHJ, its lineage from BBC Matrix-H and 45J through Gerzon, and what the plugin does (AmbiX to UHJ C-format).
   State the thesis: matrix coefficients from Gerzon plus a sample-rate-independent quadrature network, both derived and verified numerically.
2. **AmbiX to FuMa.**
   Document the pre-conversion `ambixToFuMa`: ACN/SN3D to WXYZ, with W scaled by 1/√2.
3. **The UHJ C-format matrix.**
   Present Σ, Δ, L, R, T, Q with all constants and the meaning of each channel.
4. **Reconstructing Gerzon's coefficients (inverse verification).**
   Present the localization theory (velocity vector r_V and energy vector r_E), the mono-compatibility constraint, and the 20° angle.
   Take the published coefficients and show numerically that they satisfy the localization conditions, plotting the cost landscape around them.
   Evaluate r_V and r_E under two listening models, shown side by side: surround decode (the reconstructed B-format field, Gerzon's original framework) and super-stereo (L/R on two frontal loudspeakers at ±30°, the practical E4L use).
   The surround model explains why Gerzon chose these coefficients; the super-stereo model explains what a listener hears on stereo.
   The side-by-side presentation lets a reader new to ambisonics follow §4 through the familiar stereo case.
5. **The j problem: wideband 90° phase shift.**
   Explain why the quadrature term is required, and why FIR designs tie to sample rate and tap count.
6. **The sample-rate-independent design (the contribution).**
   Present the RBJ topology and the analog s-domain prototype that fixes the 0.40° ideal.
   Show that a fixed (f, Q) set realized via the bilinear form drifts with fs, because the bilinear warping shifts the broadband phase (the spike finding).
   Present the resolution: the design procedure is fs-free, and the coefficients are derived per sample rate, stored as a precomputed table for the standard rates.
7. **Validation.**
   Present empirical-versus-analytic comparison and the multi-sample-rate plot that contrasts the drift of a fixed coefficient set against the flat error of the per-rate table, at 44.1/48/88.2/96/176.4/192 kHz.
8. **References.**
   Cite the five PDFs in `doc/references/`, including the 2023 E4L paper.

Appendices hold coefficient tables, C++ core snippets, and the optional migration path.

## Tooling architecture

All tools live in `plugins/x2uhj/tools/`, design-time only, outside the build, per CLAUDE.md.
One module maps to one concern, and each mirrors a document section.

| Module | Section | Role |
|---|---|---|
| `rbj.py` *(exists)* | §6 | canonical all-pass model, identical to the C++ core |
| `analog_prototype.py` *(new)* | §6 | s-domain all-pass, continuous phase, plus bilinear transform to each fs |
| `design_quadrature.py` *(exists, unchanged)* | §6 | digital minimax fit at 48 kHz, emits `coeffs.json` (current plugin source) |
| `design_quadrature_sdomain.py` *(new)* | §6 | minimax fit on the analog prototype, emits `coeffs_analog.json` (the s-domain 0.40° ideal) |
| `design_quadrature_perfs.py` *(new)* | §6 | run the digital phase fit at each standard rate, emit the per-rate table `coeffs_perfs.json` |
| `gerzon_verify.py` *(new)* | §4 | inverse verification: evaluate r_V/r_E on published coefficients under both listening models (surround decode and super-stereo), plot angular error per azimuth and the cost landscape |
| `validate_multifs.py` *(new)* | §7 | contrast a fixed coefficient set (drift) against the per-rate table (flat) across the standard rates, table plus plot |
| `compare_empirical.py` *(exists)* | §7 | 2023 empirical versus analytic |
| `emit_header.py` *(exists)* | — | `coeffs.json` to `x2uhj_coeffs.h` |

## Data flow

```
design (old)            ─→ coeffs.json         ─┬─→ emit_header.py ─→ x2uhj_coeffs.h (current plugin)
                                                 └─→ doc figures (the fixed-coefficient design)
design (s-domain)       ─→ coeffs_analog.json   ─→ doc figures (the 0.40° s-domain ideal)
design (per-rate fit)   ─→ coeffs_perfs.json    ─→ doc figures (the fs-independent table)
gerzon coeffs (published) ─→ gerzon_verify.py   ─→ localization figure (§4)
fixed set vs per-rate table ─→ validate_multifs.py ─→ multi-fs drift-vs-flat plot (§7)
```

`coeffs.json` stays the single source of truth for the shipped plugin in this work.
`coeffs_analog.json` records the s-domain ideal that motivates the contribution.
`coeffs_perfs.json` is the per-rate table the future plugin fix consumes; this work produces and documents it without modifying the plugin.

## Risks and mitigations

1. Gerzon's published coefficients may sit slightly off an exact minimum of our cost.
   We document the gap and interpret it through implicit constraints and Matrix-H heritage; the inverse-verification choice already accommodates this outcome.
2. A fixed coefficient set realized via the bilinear form drifts with sample rate (the spike finding).
   The multi-fs plot turns this into the central result: the fixed set drifts while the per-rate table stays flat.
3. The per-rate table covers the standard rates; an unusual rate falls back to the nearest table entry.
   The document states this limitation, and the closed-form prewarp-remap appears as a documented approximation (adequate for fs ≥ 48 kHz, 12° at 44.1 kHz).
4. Python dependencies (scipy, matplotlib) already exist in the `.venv`.
   We update `requirements.txt` to match.

## Testing and verification

- Each script runs cleanly in the `.venv` and emits its figures without errors.
- `coeffs_analog.json` reloads and reproduces identical numbers (determinism).
- C++ and Python agree: `rbj.py` and `AllpassSection` produce identical coefficients, made an explicit numeric check.
- The LaTeX compiles to PDF with all warnings resolved.

## Success criteria

- A student reads the PDF and follows, step by step, the path from AmbiX to L/R/T/Q, including why Gerzon's coefficients hold and why the quadrature design is sample-rate independent only as a per-rate procedure.
- A reader new to ambisonics follows §4 through the super-stereo model, while a reader with ambisonic background reads the surround model; the two presentations agree on the conclusion.
- The §7 plot makes the spike finding visible: a fixed coefficient set drifts with fs while the per-rate table stays flat.
- Every figure, coefficient, and the per-rate table regenerate from one command.

## Migration-path note

The plugin keeps shipping `coeffs.json` in this work.
The per-rate table lands as `coeffs_perfs.json`, and the s-domain ideal as `coeffs_analog.json`.
The migration-path appendix records how a future plugin selects the table entry by sample rate, and how an unusual rate falls back to the nearest entry.
The plugin C++ change is a separate follow-up with its own spec and audio testing, consistent with the SEAM porting convention.
