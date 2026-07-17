# calbus UI follow-ups — strx layout, adaptive spectrum, emitter controls

Date: 2026-07-17
Author: Giuseppe Silvi + Claude
Status: design approved, ready for implementation planning

Follows `docs/superpowers/specs/2026-07-16-calbus-peer-aware-bus-design.md` (Spec 2, implemented on branch `calbus`).
Everything here comes from GS's in-host verification of that work in Reaper.

---

## 1. Origin

The calibration bus works.
The in-host session traced the whole lifecycle across three separate `.vst3` bundles: `calbus: no emitter` → `1 idle, none sounding` → `multipink · STONE ? · slot 0-3 · -23.0 dB` → `ltglide · pass 1` → back to idle.
That is Spec 2's Task 5 Step 7, and it passes.

Using it surfaced four things, three of them GS's and one found while checking his screenshots.

**The status line is truncated.** The view is 300 px at 11 px Source Code Pro (~6.6 px/char, ~45 chars). The longest line, `ltglide · STONE ? · pass 7 · 20000→20 Hz · no host clock`, is 56 chars. It is cut at `· no`. The `· +N more` flag added at the end of Spec 2 — whose entire purpose is to warn that two emitters are sounding at once — sits past the cut and would never be seen in the room.

**The spectrum is wrong for a sweep.** Welch with a 2 s EMA (`strx_dsp.h:47`) is excellent for pink noise and useless for ltglide: a sweep presents one frequency at a time, so the average sees a moving peak and smears it.

**`STONE ?` is honest but unusable.** `kParamStoneId` exists and is automatable, but neither emitter has a control for it in its `.uidesc`, so it is reachable only through the host's generic parameter list. This was deferred minor (h) in Spec 2's final review, explicitly left as GS's call.

**ltglide cannot fire a single pass.** With LOOP off, `GlideTransport::process()`'s `Idle` case does `if (loop_) beginPass(); else return Silence` — there is no other exit, so LOOP off means permanent silence. `trigger()` exists, is public, guards correctly on `state_ == Idle`, and has never been wired to anything but the unit test.

**And one found here, not in the room:** `libseamcalbus.dylib` is `x86_64` only while the plugins are `x86_64 arm64`. See §7.

## 2. Scope

Three plugins and one build fix:

- **strx** — layout, visual language, adaptive spectrum.
- **ltglide** — SHOT button, STONE control.
- **multipink** — STONE control.
- **libseamcalbus** — universal binary.

Out of scope: the transfer function and Δt (Spec 3); auto-EQ (Spec 4); any change to the bus ABI, the seqlock, or the record layout.

## 3. strx layout

Dimensions emerge from the quadrants that already exist rather than from dslar's.
The goniometer stays 260 wide, the meters 270, and the window width is their sum: `20 + 260 + 30 + 270 + 20 = 600`.

| Element | x | y | size |
|---|---|---|---|
| Title / subtitle | 0 | 14 / 42 | 600 × 26 / 600 × 18 |
| Goniometer view | 20 | 70 | 260 × 300 |
| Meters view | 310 | 70 | 270 × 300 |
| Status line | 20 | 380 | 560 × 26 |
| Spectrum view | 20 | 416 | 560 × 240 |
| Logo | 180 | 670 | 240 × 77 |

Window: **600 × 770**, fixed (`minSize` = `maxSize`, as today).

Each top view splits internally into three bands, and the split is what makes the alignment automatic:

| Band | y within view | Goniometer | Meters |
|---|---|---|---|
| Labels | 0–18 | `L` `R` | `L` `R` `M` `S` `W` |
| Plot | 18–278 | circle + scatter + needle | bars |
| Values | 278–300 | `ANGLE +44° PANORAMA -1%` | `-37.3` … `wide` |

The plot band is 260 px in both, at the same absolute y (88–348), so the circle and the bars are the same height and align without anyone imposing it.

This is the whole of "labels outside the frames" and "meters as tall as the goniometer": today the text lives *inside* the plot area and eats it from within, which is why the bars never reach the circle's height. Moving the text out is what frees the geometry.

**The status line grows from 45 to 84 characters.** The 56-char worst case fits with room for `· +N more`. This also makes the host-clock question readable — see §9.

**The spectrum grows from 310 to 560 px**, nearly doubling the horizontal resolution of the 20 Hz–20 kHz log axis. That is the real gain of the new proportion.

The spectrum's own axis labels stay inside its plot frame: there they are part of the graph, not labels on the panel.

## 4. Visual language (strx is the pilot)

Two changes, tested here before being written up as a suite-wide guideline.

**No grey backdrop.** `strx_goniometer.h:92-93` (and its siblings) fill the whole view rect with `SliderTrack` `#444444`. That fill goes; `BgDark` `#292c2f` shows through, and the plugin background becomes the objects' background. With no backdrop there are no frames at all — what delimits each plot is its own geometry: the circle, the bar tracks, the axes.

**One white point for text.** Every string renders in `TextLight` `#fcfbfd`. No string in strx uses a second colour.

**Structure stays quiet.** The circle, the radial grid, the axes and the bar tracks are structure, not writing, and keep their dim greys. The hierarchy is deliberate: the data (gold scatter, azure needle, white text) is luminous and the scaffolding accompanies it.

**The palette gains one name.** `#888888` is currently called `TextDim`, and after this change nothing in strx draws *text* with it — it draws circles, grids and axes. A colour named for a role it no longer has is how the next reader gets misled, so strx adds `Structure` `#888888` and uses that. `TextDim` stays in the palette untouched: other plugins still use it as a text colour, and retiring it is a question for the guideline, not for this pilot. `SliderTrack` `#444444` keeps its name and its job (the bar tracks are literally tracks).

**One consequence to check in the host.** The radial grid is `#888888` and today sits on the `#444444` backdrop — roughly 2:1 contrast. On `BgDark` it becomes roughly 4:1, so *the same grid will read more strongly than it does now* without any colour having changed. If it competes with the scatter, the fix is to darken the structure (≈`#666666`), not to bring the backdrop back. This is a review point for GS's eyes, not a code decision.

The palette entries themselves are unchanged; other plugins still use `TextDim`. Whether it survives suite-wide is a question for the guideline, after this pilot.

## 5. Adaptive spectrum

The display and the measurement are separate concerns, and this section is only about the display.
Spec 3 will not read these curves: it deconvolves the regenerated reference against the capture to get an impulse response, and `H` from that.
The spectrum exists for the operator's eyes during a pass.

**Max-hold is nearly free and needs no mode.**
`Welch::runFrame` already computes a per-frame power `p` and folds it into an EMA.
Adding `holdLin_[k] = max(holdLin_[k], p)` is one line, costing one `max` per bin per frame — with hop 2048 that is ~23 frames/s, ~47k operations per second. The hold is therefore computed **always**, and the GUI decides whether to draw it.

**τ is the only thing that must switch.**
`strx_dsp.h:47` sets τ = 2 s. That constant is what makes pink noise read smoothly and exactly what smears a sweep: on a 20 s pass it blurs 10% of the sweep, giving a bump rather than a peak. In glide mode τ drops to 100 ms so the live curve tracks the sweep.

**What is drawn:**

| Bus says | Curves |
|---|---|
| pink active, no emitter, or bus unavailable | M and S Welch EMA (τ = 2 s) — exactly today's behaviour |
| glide active | M and S max-hold (full weight) + M and S live (τ = 100 ms, tenuous) |

Four curves in glide mode, two in pink mode. The hold resets at each new pass.

**When more than one emitter is active**, the spectrum follows the same record the status line names — the first active one in the snapshot. They read the one cached accessor below, so they cannot disagree about which emitter is "the" emitter. This is not a real configuration by method (one sounds at a time) and the status line flags it with `· +N more`; the rule exists so the two views stay consistent when the method is violated, rather than the spectrum silently picking a different emitter than the line names.

**Flow, and how it keeps Spec 2's rule.** Spec 2 contracted that strx's audio thread never touches the bus. It still does not:

1. The GUI reads the bus and writes two atomics on the processor: `busMode` (pink/glide) and `holdEpoch` (bumped when `passCounter` changes).
2. The DSP reads them **at frame boundaries**, not per sample. Mode change → set τ. `holdEpoch` change → `resetHold()`.

`AnalysisFrame` gains `holdM[kNumBins]` and `holdS[kNumBins]`, growing from ~16 KB to ~33 KB per frame — about 1 MB/s of copying at 30 Hz. Irrelevant.

**One cached bus accessor, two readers.** The status line and the spectrum both want the bus now. Two independent readers sampling at different rates would disagree at the same instant — the line saying `pass 7` while the spectrum is still clearing for pass 6. A single cached accessor on the processor, which both views interrogate, prevents that. This is the same lesson `reference_lockfree_spsc_triplebuffer` records for the goniometer and the meters, applied to the bus.

## 6. Emitter controls

**ltglide SHOT button** — a custom `CView`, no VST3 parameter of its own, and none driven either.

`DslarResetButton` is "UI-only" in that it owns no parameter, but it drives six real ones through `controller_->performEdit()` (`dslar_reset_button.h:88-119`), so it still goes through the host. SHOT has no such road: `trigger()` is internal transport state, and there is nothing for a host to automate. The button therefore speaks to the processor directly through two atomics — `shotRequest_` (GUI → DSP, read and cleared with an `exchange(false)` in `process()`) and `transportRunning_` (DSP → GUI, which lights the button). This works because `SingleComponentEffect` makes the processor and the controller the same object, exactly as strx's views already read the analyzer.

It is also the safest shape against the momentary-button coalescing problem recorded in `reference_vst3_momentary_button_and_vstgui_attrs`: there is no parameter for a host to coalesce.

Behaviour: press → one pass starts and the button stays azure (`SliderActive` `#4a9ec8`) for its whole duration (~32 s: head Dirac + 5 s lead + 20 s sweep + 5 s tail + tail Dirac), then goes dark. Pressed during a pass, or with LOOP on, it does nothing — `trigger()`'s existing `if (state_ == State::Idle)` guard already provides this, and it is the right behaviour: a truncated pass would still publish its `passCounter` and anchor, and Spec 3 would average it as if it were whole.

**STONE control** on both emitters — a `COptionMenu` bound to `kParamStoneId`, mirroring `multipink.uidesc:34`, which already presents the 3-step `Reference` `StringListParameter` that way. Nine entries: `?`, `1`…`8`, default `?`.

## 7. Universal `libseamcalbus`

```
this machine:     x86_64
multipink.vst3:   x86_64 arm64      ← universal
libseamcalbus:    x86_64            ← Intel only
```

The VST3 targets get `SMTG_BUILD_UNIVERSAL_BINARY` (default ON), which sets `XCODE_ATTRIBUTE_OSX_ARCHITECTURES "x86_64;arm64;arm64e"` (`cmake/modules/SMTG_UniversalBinary.cmake:22`).
`seam_calbus` is our own `add_library`, so it inherits `CMAKE_OSX_ARCHITECTURES`, which the root `CMakeLists.txt:15-18` pins to the host — Intel here.

On an Apple Silicon machine the plugins would load natively as arm64 and the dylib would not load at all: strx would read `calbus unavailable` and the bus would simply not exist. It degrades exactly as §8 of the Spec 2 design contracts, and it is visible rather than silent — but it is guaranteed on any ARM machine, not hypothetical.

Spec 2's final review triaged this as deferred minor (e), "fix-later". The diagnosis was right and the priority was wrong: it bites the moment the work leaves this machine, which is precisely what GS wants to do (Nuendo, on another machine — confirmed not installed here).

Fix, one line in `plugins/_common/calbus/CMakeLists.txt`:

```cmake
smtg_target_setup_universal_binary(seam_calbus)
```

This is the SDK's own public function (`SMTG_UniversalBinary.cmake:16`), so the dylib and the plugins cannot drift: if the SDK's policy changes, they change together. A hand-written `set_target_properties` would be the copy that falls behind.

## 8. Testing

The DSP core stays SDK-free and unit-tested in `tests/`:

- `Welch`'s max-hold: it holds the maximum, `resetHold()` clears it, and the EMA path is unchanged. Verify by mutation that each assertion can fail.
- τ switching: changing the mode changes the EMA time constant and nothing else.

The rest is GUI and must be verified in the host — there is no automated substitute:

1. Load multipink, set STONE 2 from its new control, un-mute. strx names `multipink · STONE 2 · slot 0-3 · -23.0 dB`, untruncated.
2. Load a second multipink, un-mute both. The line flags `· +N more` — the check that was impossible before, since the flag sat past the 45-char cut.
3. Swap for ltglide, LOOP off. Press SHOT: one pass fires, the button stays azure for its duration, and the spectrum's max-hold draws the response curve as the sweep descends, with the live curve showing where it is. At the next SHOT the hold clears.
4. Press SHOT during a pass: nothing happens.
5. Read the tail of the ltglide line — see §9.
6. Visual: the grid's new contrast against `BgDark` (§4).

VST3 validator on all three plugins, failure count 0.

## 9. The open question this unblocks

Whether Reaper supplies `kContTimeValid` is still unknown, and the truncation is why.

GS's ordered sequence shows `pass 1` starting with the transport rolling — which is the answer — but the crop cuts exactly where `T=20s` or `no host clock` would be. An earlier screenshot showing `· no` was of a pass started while stopped, so its `-1` anchor was correct and stale, and says nothing about Reaper's behaviour while rolling. (ltglide free-runs: Reaper calls `process()` continuously for monitoring, so its transport grinds passes regardless of the host's.)

The widened status line makes this readable without another change. If it reads `no host clock` for a pass started while rolling, then Reaper never supplies the flag and Spec 3 must rest on `projectTimeSamples`, which the SDK guarantees "always valid" (`ivstprocesscontext.h:124`) but which jumps on loops and relocation. That is a Spec 3 decision and is out of scope here.

## 10. Decisions recorded

- The spectrum shows max-hold **and** a live curve, over max-hold alone: seeing where the sweep is and what it has already measured at once is worth the four curves.
- The hold resets per pass, over accumulating across passes: max does not average, it converges upward, so a persistent hold would not be the visual analogue of Spec 3's pass averaging.
- SHOT does not stop a running pass: a truncated pass still publishes a `passCounter` and an anchor, and Spec 3 would average it as valid.
- Structure keeps its dim greys while text unifies to one white: "one white point" was GS's requirement for *writing*; the circle, grid and axes are scaffolding, and making them as luminous as the data would defeat the hierarchy the change is for.
- Dimensions emerge from the existing quadrants rather than copying dslar's 460×570.
