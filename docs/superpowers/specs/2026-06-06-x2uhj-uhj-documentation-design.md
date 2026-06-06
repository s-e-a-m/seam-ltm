# X2UHJ — UHJ Mathematics Documentation (Design Spec)

Date: 2026-06-06
Status: Approved (brainstorming), pending spec review
Owner: Giuseppe Silvi (grammaton)

## Purpose

Produce a standalone LaTeX document that explains the mathematics of the `x2uhj` plugin step by step.
The document serves SEAM students as a *Learning Through Making* artifact.
It also serves as the complete mathematical deposit from which a later integrative paper extracts.

The document carries two derivations.
The first derivation covers Gerzon's UHJ C-format matrix coefficients, verified numerically by inverse verification.
The second derivation covers a sample-rate-independent quadrature all-pass network, the original contribution of this work.

## Scope

### In scope

- A LaTeX PDF in `plugins/x2uhj/doc/`, written in English.
- Writing follows two house rules: one sentence per line, and affirmative explanatory voice.
- Coverage spans the full UHJ C-format: L, R, T, Q.
- New design-time Python tools under `plugins/x2uhj/tools/`.
- Regenerable figures under `plugins/x2uhj/doc/figures/`.
- An optional migration-path appendix, included when the analog design proves measurably better.

### Out of scope

- The peer-review paper itself, which becomes a later extract with its own spec.
- Changes to the plugin C++ DSP; `coeffs.json` stays intact until a separate migration decision.
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
6. **The sample-rate-independent all-pass network (the contribution).**
   Present the RBJ topology, the analog s-domain prototype, the derivation of (f, Q), the minimax fit, and the bilinear transform per sample rate.
7. **Validation.**
   Present empirical-versus-analytic comparison, phase error, and the multi-sample-rate tables and plots at 44.1, 48, 96, and 192 kHz.
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
| `design_quadrature_sdomain.py` *(new)* | §6 | minimax fit on the analog prototype, emits `coeffs_analog.json` |
| `gerzon_verify.py` *(new)* | §4 | inverse verification: evaluate r_V/r_E on published coefficients under both listening models (surround decode and super-stereo), plot angular error per azimuth and the cost landscape |
| `validate_multifs.py` *(new)* | §7 | phase error at 44.1/48/96/192 kHz, table plus plot |
| `compare_empirical.py` *(exists)* | §7 | 2023 empirical versus analytic |
| `emit_header.py` *(exists)* | — | `coeffs.json` to `x2uhj_coeffs.h` |

## Data flow

```
design (old)          ─→ coeffs.json        ─┬─→ emit_header.py ─→ x2uhj_coeffs.h (current plugin)
                                              └─→ doc figures (current design)
design (new, s-domain) ─→ coeffs_analog.json ─→ doc figures (proposed design)
gerzon coeffs (published) ─→ gerzon_verify.py ─→ cost-landscape figure (§4)
both designs            ─→ validate_multifs.py ─→ multi-fs table + plot (§7)
```

`coeffs.json` stays the single source of truth for the shipped plugin.
The design chain `design_quadrature.py → coeffs.json → emit_header.py → x2uhj_coeffs.h` keeps the document, its figures, and the production DSP derived from one file.
The new analog design writes a separate `coeffs_analog.json` and touches the plugin only after an explicit human migration decision.

## Risks and mitigations

1. Gerzon's published coefficients may sit slightly off an exact minimum of our cost.
   We document the gap and interpret it through implicit constraints and Matrix-H heritage; the inverse-verification choice already accommodates this outcome.
2. Residual bilinear warping appears across extreme sample rates (44.1 versus 192).
   The multi-fs table quantifies it; a visible residual is a result.
3. Python dependencies (scipy, matplotlib) already exist in the `.venv`.
   We update `requirements.txt` to match.

## Testing and verification

- Each script runs cleanly in the `.venv` and emits its figures without errors.
- `coeffs_analog.json` reloads and reproduces identical numbers (determinism).
- C++ and Python agree: `rbj.py` and `AllpassSection` produce identical coefficients, made an explicit numeric check.
- The LaTeX compiles to PDF with all warnings resolved.

## Success criteria

- A student reads the PDF and follows, step by step, the path from AmbiX to L/R/T/Q, including why those coefficients hold and how the all-pass stays sample-rate independent.
- A reader new to ambisonics follows §4 through the super-stereo model, while a reader with ambisonic background reads the surround model; the two presentations agree on the conclusion.
- Every figure and coefficient regenerates from one command, from the single source.

## Migration-path note

The plugin keeps shipping `coeffs.json` for project compatibility.
The new s-domain design lands as `coeffs_analog.json` alongside it.
When the multi-fs validation shows a measurable advantage for the analog design, the document gains a migration-path appendix that records how to move the plugin to `coeffs_analog.json`.
This migration stays a deliberate human review step, consistent with the SEAM porting convention.
