# All-Pass Network Topologies for Wideband 90° Quadrature in Audio DSP, with Focus on Sample-Rate Behaviour and UHJ C-Format Encoding

## TL;DR
- The researcher's central distinction — that sample-rate independence of a quadrature all-pass pair is a property of the **design procedure** (re-fitting coefficients at the actual fs), not of a stored coefficient set, and that storing physical (f,Q) pairs and recomputing biquads via the bilinear transform **drifts** while re-running a minimax fit **holds** — is **novel as an explicit, quantified, multi-rate comparative statement in the UHJ context**, even though every individual mechanism it rests on is already established.
- The literature splits cleanly: classic analog/RF phase-difference networks (Saraga, Darlington, Weaver, Bedrosian) and the digital all-pass canon (Regalia–Mitra–Vaidyanathan, Ansari, Lang, Harris–Berdahl–Abel) specify designs in **normalized frequency**, so digital coefficients transfer across sample rates while the *protected Hz band scales with fs*; none of this canon frames the (f,Q)-recompute-versus-redesign drift as a problem to be measured for a fixed audio band.
- For UHJ specifically, real implementations diverge: the Ambisonic Toolkit uses a **single sample-rate-independent FIR/IR kernel** (folder literally named `None`, with the source comment "IR in UHJ decoding does not depend on sample rate"); Wiggins's WigWare uses IIR all-pass phase-shift networks; no published UHJ implementation explicitly measures or solves the quadrature-versus-sample-rate problem with a minimax-fit-per-rate validation harness — that absence is the gap.

## Key Findings

1. **Two dominant digital topologies.** The wideband 90° quadrature problem is realised either as (a) a **two-path polyphase IIR network** of first-order all-pass sections in z⁻² (the Niemitalo / de Soras / Ansari / Harris lineage, derived from an elliptic half-band prototype), or (b) a **cascade of second-order all-pass biquads** per path (the RBJ-style realisation the researcher uses). The polyphase form is the most computationally efficient and the dominant published structure; the biquad-cascade form is more common in parametric/physical-parameter workflows where designs are stored as (f,Q).

2. **Normalized-frequency design is the historical norm.** Both the analog phase-difference literature (S. D. Bedrosian, "Normalized Design of 90 Degree Phase-difference Networks," *IRE Transactions on Circuit Theory*, June 1960, pp. 128–136) and the digital halfband/Hilbert literature (Ansari 1987) specify the design in dimensionless frequency. In the analog case the prototype is frequency-scaled once to the band; in the digital case the spec is a fraction of fs, so coefficients transfer but the protected Hz band moves with fs. Neither framing addresses holding a *fixed Hz band* (20 Hz–20 kHz) invariant across fs.

3. **The bilinear-warping drift the researcher measured is real and predictable.** RBJ biquads are analog prototypes mapped by the bilinear transform with prewarping at the centre frequency only. A fixed (f,Q) set recomputed at a new fs prewarps correctly at each section's centre but not across the broadband phase difference, so the *difference* of two cascades drifts — consistent with the researcher's 1.36°→22°→47°→54° measurements (48/44.1/96/192 kHz). This is a known property of the bilinear transform, but its specific consequence for a quadrature *pair* in UHJ is not quantified anywhere I found. Practitioners have noticed the symptom informally: on the KVR Audio forum a polyphase-Hilbert discussion (kvraudio.com thread t=391757) records "With all-pass filter banks you won't get an exact 90 degree difference in the lowest and highest frequencies … I don't think that they will work well at low frequencies without frequency warping," and "the higher the samplerate – the worse the error range."

4. **Re-running the fit per fs restores quadrature; oversampling is the dual.** Because the design sections sit far below Nyquist, the digital realisation approaches the s-domain ideal (the researcher's 0.40°) as fs rises — exactly why internal oversampling is a coherent alternative to per-rate redesign. The oversampling-for-phase-accuracy idea exists in the patent literature (decimation/phase-correction in oversampled environments, giving substantially linear phase over the band of interest) but is not developed for the UHJ quadrature pair.

5. **UHJ implementations are split and none solves the stated problem explicitly.** ATK = single sample-rate-independent kernel; WigWare = IIR all-pass; Gerzon's originals = analog all-pass networks (the "j" operator); FIR-Hilbert approaches = sample-rate-dependent tap sets. No published UHJ work presents a comparative topology ranking under a multi-rate validation harness.

## Details

### A. Topology Taxonomy

| Topology | Canonical design method | Phase error vs order | Group delay | CPU cost | Sample-rate behaviour | Key references |
|---|---|---|---|---|---|---|
| **Two-path polyphase IIR** (first-order all-pass in z⁻², two parallel cascades, one with unit delay) | Elliptic/Jacobian half-band prototype → pole transformation → pole interlacing; or evolutionary/numeric fit | Excellent: Niemitalo's published 8-multiply pair holds "90 +/- 0.7 degrees over a band of width 0.998·Nyquist"; pushes toward fractions of a degree at higher order | Low, non-linear; roughly flat in the protected band | Lowest (one multiply per section) | Designed in **normalized** frequency about Nyquist/2; coefficients transfer across fs but the protected Hz band scales with fs. To hold a fixed Hz band, redesign/warp per fs | Ansari 1987; Regalia–Mitra–Vaidyanathan 1988; Harris–Berdahl–Abel 2010; de Soras HIIR; Niemitalo 2003 |
| **Second-order all-pass biquad cascade** (RBJ APF per path) | Place (f,Q) pairs spanning the band; bilinear transform with prewarp per section | Good; depends on number/placement of sections; researcher reaches ~2.04°@44.1k down to 0.42°@192k with 3 sections/network after per-fs fit | Non-linear, lumpy near section centres | Moderate (biquad per section) | **Drifts** if (f,Q) fixed and biquad recomputed per fs (bilinear warps the broadband phase difference); **holds** if coefficients re-fit per fs. This is the researcher's core empirical result | RBJ Audio EQ Cookbook; Lang 1998; Regalia–Mitra 1987 |
| **Half-band / elliptic-derived polyphase decomposition** | H(z)=A₀(z²)+z⁻¹A₁(z²); design lowpass half-band, transform poles to Hilbert | Equiripple-like; tied to elliptic order | Low | Very low (multiplierless variants exist) | Normalized to fs/4; same transfer-but-scale behaviour | Ansari 1985; Schüßler–Steffen 1998; Harris–Berdahl–Abel 2010 |
| **Minimax / equiripple all-pass phase fit** (Remez/eigenvalue) | Generalized Remez or eigenvalue iteration to minimise max phase-difference error | Best error-per-order in Chebyshev sense | Non-linear | Design-time cost; runtime same as cascade | Procedure runs at a chosen fs; **re-running at actual fs gives sample-rate independence** — the researcher's procedure | Lang 1998; Lang–Laakso 1994; Zhang–Iwakura 1999 |
| **FIR Hilbert transformer** | Windowed/Remez antisymmetric FIR; or analytic complex half-band FIR | Arbitrarily good with taps; linear phase | High, constant (linear phase) — bulk delay | High (many taps for 20 Hz reach) | **Tap set is fs-specific**; must be regenerated per fs (ATK stores per-rate kernels for HRTF; UHJ uses one sample-rate-independent kernel) | Standard FIR design; ATK kernels |
| **Thiran / fractional-delay all-pass** | Maximally-flat group-delay all-pass | For delay, not quadrature directly; usable in delay-vs-allpass complementary pairs | Maximally flat delay | Low | Normalized; used in variable/fractional contexts | Thiran; Välimäki; Laakso et al. |

#### Per-topology prose

**Two-path polyphase IIR.** This is the workhorse. The basic section is H(z)=(a²−z⁻²)/(1−a²z⁻²), one multiply when a² is precomputed; two cascades with a one-sample relative delay produce outputs ~90° apart. It descends from elliptic half-band filters (Ansari) and is implemented in de Soras's HIIR and popularised for audio by Niemitalo, whose own published specification (comp.dsp, 2003) is: "Takes 8 multiplications per input sample · Phase difference 90 +/- 0.7 degrees over a band of width 0.998·Nyquist." It is defined in normalized frequency symmetric about Nyquist/2; practitioners explicitly note the low-frequency error worsens at higher sample rates if the design is not warped — the same phenomenon the researcher quantifies, observed informally rather than measured systematically.

**Second-order all-pass biquad cascade (RBJ).** RBJ's all-pass biquad H(s)=(s²−s/Q+1)/(s²+s/Q+1) is mapped by the bilinear transform with prewarping at w0 only. Two such cascades whose phase difference targets 90° are exactly the researcher's H_R/H_I. The cookbook is explicit that BLT warping is compensated at the centre frequency and for bandwidth, but nothing guarantees the broadband *phase difference* of two cascades is preserved when fixed (f,Q) pairs are re-warped at a new fs — hence the measured drift. This is the crux of the researcher's contribution.

**Half-band/elliptic polyphase.** Harris, Berdahl & Abel (AES Convention 129, Paper 8258, 2010) give a clean, numerically robust procedure: design an IIR half-band in parallel-allpass form, move poles to make a Hilbert transformer, warp to the band, realise as cascaded first-order all-passes. The technique "is based entirely on pole locations, and creates a numerically robust filter in cascaded first-order allpass form," and their worked example shows the phase "leaves its approximated region at 2 kHz and 18 kHz" — i.e., the protected band is set by an explicit warping step where fs enters. Their Step 3 ("apply frequency warping to achieve the desired frequency range") is precisely the redesign operation, supporting the researcher's "procedure-not-coefficients" framing.

**Minimax all-pass fit.** Lang (1998) and Lang–Laakso (1994) solve the Chebyshev phase-approximation/equalisation problem via a generalized Remez algorithm with guaranteed convergence; Zhang & Iwakura (1999) give an eigenvalue method with equiripple phase, demonstrated explicitly on Hilbert transformers. These are the formal basis for "re-run a minimax digital phase fit at the actual sample rate."

**FIR Hilbert.** Linear phase, arbitrarily accurate, but the tap set is fs-specific and the bulk delay/CPU for a 20 Hz reach is large. This is the "Farina-style" sample-rate-dependent route. ATK stores per-rate FIR kernels for HRTF decoders but — importantly — uses a *single* sample-rate-independent kernel for UHJ.

### B. Foundational references

- **Analog phase-difference networks:** Saraga (1950), Darlington (1950), Orchard (1950), Weaver (1954), Bedrosian (1960). Bedrosian's "normalized design" — design curves in dimensionless frequency parameterised by bandwidth ratio (covering a 2000:1 range), re-scaled once to the band — is the conceptual ancestor of frequency-scalable quadrature design.
- **Digital all-pass canon:** Regalia, Mitra & Vaidyanathan, "The Digital All-Pass Filter: A Versatile Signal Processing Building Block," Proc. IEEE 76(1), 19–37, 1988 (DOI 10.1109/5.3286) — the survey whose abstract lists applications in "notch filtering, complementary filtering and filter banks, multirate filtering, spectrum and group-delay equalization, and Hilbert transformations"; Vaidyanathan, Regalia & Mitra (IEEE TCAS 1987) on doubly-complementary IIR via a single complex all-pass; Regalia & Mitra (IEEE ASSP 1987) on tunable equalizers.
- **Hilbert-specific:** Ansari, "IIR Discrete-Time Hilbert Transformers" (IEEE Trans. ASSP-35, 1987); Ansari, "Elliptic Filter Design for a Class of Generalized Halfband Filters" (IEEE Trans. ASSP-33, 1985); Schüßler & Steffen, "Halfband Filters and Hilbert Transformers," Circuits, Systems and Signal Processing 17(2), 137–164, 1998 (DOI 10.1007/BF01202851); Harris, Berdahl & Abel (AES 129, 2010).
- **All-pass phase design:** Lang, "Allpass Filter Design and Applications," IEEE TSP 46(9), 2505–2514, 1998; Lang & Laakso (1994); Zhang & Iwakura (1999).
- **Practitioner:** Niemitalo (2003, yehar.com / comp.dsp); de Soras HIIR; RBJ Audio EQ Cookbook.

### C. UHJ-specific prior art

- **Gerzon, "Ambisonics in Multichannel Broadcasting and Video," JAES 33(11), 859–871, Nov. 1985** (AES e-lib ID 4419; also AES Preprint 2034, 74th Convention, 1983) — the canonical UHJ encoding matrix with the "j" 90° operator; appendices give encode/decode equations. Gerzon's hardware realised "j" as analog all-pass phase-difference networks. Earlier theory in Gerzon (AES 56th Convention, 1977).
- **Ambisonic Toolkit (ATK)** — UHJ encode/decode via convolution kernels. Verified from the atk-reaper source (github.com/ambisonictoolkit/atk-reaper, FOA/Decode/UHJ): the UHJ kernel path is literally `…/uhj/None/<kernelSize>/…` with the comment "We do not need to resample, as IR in UHJ decoding does not depend on sample rate." The per-rate kernel families (44100/48000/88200/96000/176400/192000) exist in ATK only for the HRTF/binaural decoders (CIPIC, Listen), which are genuinely sample-rate-dependent. Reference: Lossius & Anderson, "ATK Reaper: The Ambisonic Toolkit as JSFX plugins," ICMC/SMC 2014.
- **Wiggins WigWare UHJ Encoder/Decoder/Transcoder** (Reaper JSFX, brucewiggins.co.uk) — uses "Allpass filter based phase shift networks. This is similar to the techniques used in the original hardware, and can sound more natural that FIR filter alternatives," based on Gerzon 1985. Whether its coefficients are hard-coded or recomputed from `srate` could not be verified from public source (the JSFX is distributed only inside a ZIP, not web-indexed); the standard JSFX all-pass idiom would recompute the discrete coefficient from `tan(π·fc/srate)`, implying per-rate adaptation from fixed normalized pole frequencies, but this is inference.
- **FIR-Hilbert UHJ** (convolution) — the Ambisonics FAQ / XiphWiki note that in the digital domain the 90° shifters "are usually implemented as convolution filters," i.e., sample-rate-dependent tap sets.

No published UHJ implementation explicitly addresses *and measures* sample-rate independence of the quadrature with a per-rate redesign harness.

### D. Gap analysis (skeptical)

**Verdict: partially-covered mechanisms, novel synthesis.**

- That a quadrature all-pass design is specified in normalized frequency and must be re-warped/redesigned to hold a fixed Hz band across sample rates: **already known** (Bedrosian normalized design; Ansari normalized digital spec; Harris et al. Step 3 "apply frequency warping"). Practitioners note informally that fixed-coefficient polyphase Hilberts degrade at the band edges and with rising fs.
- A *comparative methodology* that ranks **different all-pass topologies** on one footing — phase error vs order vs CPU vs sample-rate robustness — using a single minimax fit plus multi-rate validation harness: **not found**. The closest are single-topology design papers (Harris et al., Lang) and the all-pass survey (Regalia–Mitra), none of which run a cross-topology, multi-fs bake-off.
- The specific articulation — storing (f,Q) and recomputing the biquad per fs (which drifts) **versus** redesigning coefficients per fs (which holds), with measured degrees of error at 44.1/48/96/192 kHz, in the UHJ context: **novel as an explicit, quantified statement.** The underlying cause (bilinear warping of a broadband phase difference) is textbook; the framing as a UHJ-quadrature reproducibility property, with numbers, is the researcher's contribution.

Honest caveat against over-claiming: this is a clarifying/integrative contribution, not a new filter-design theorem. The "design procedure, not coefficient set" insight will read as obvious to a DSP theorist but is genuinely absent from the UHJ literature and is useful pedagogically.

### E. Oversampling angle

The s-domain prototype reaching 0.40° while the digital realisation approaches it as fs rises is the mathematical basis for internal oversampling: run the quadrature network at N×fs so its sections sit proportionally further below Nyquist, then decimate. Trade-offs: cost and latency of the up/down-sampling half-band filters (themselves polyphase all-pass), and added group delay — versus the essentially free per-rate redesign (a design-time cost only). The patent literature treats oversampling for phase-angle correction with substantially linear phase over the band of interest, but no audio/UHJ source develops the oversampling-versus-redesign trade-off for the quadrature pair. For a real-time Faust implementation, per-rate redesign is cheaper at runtime; oversampling is only attractive if the network must share an already-oversampled signal path.

## Recommendations

1. **Frame the paper's contribution precisely and modestly:** as the first explicit, *quantified*, multi-sample-rate comparison of quadrature all-pass topologies in the UHJ context, and as a pedagogical clarification that sample-rate independence is a property of the design procedure. Do not claim a new design method — cite Lang (minimax) and Harris–Berdahl–Abel (half-band→Hilbert) as the methods you apply.
2. **Position against the right prior art:** Regalia–Mitra (survey), Ansari (Hilbert/normalized), Bedrosian (analog normalized design), Lang (minimax), Niemitalo/de Soras (practitioner polyphase), Gerzon 1985 (UHJ "j"). Explicitly cite the ATK `None`-kernel fact as the contrasting "single sample-rate-independent kernel" precedent — and note that it sidesteps the problem by using FIR convolution rather than solving it for an IIR network.
3. **Stage the validation harness as the methodological core:** one minimax phase-difference fit re-run at each fs (44.1/48/88.2/96/176.4/192 kHz), reporting max error per topology per rate and CPU per sample — this is the comparative table nobody has published.
4. **Benchmarks that would change the recommendation:** if a search of the full AES e-library and IEEE Xplore surfaces a published cross-topology multi-fs bake-off (it did not in this review), downgrade the novelty claim to "replication/extension." If per-rate minimax redesign of the biquad cascade cannot beat ~1° at 44.1 kHz with ≤3 sections, switch to the polyphase half-band form, which has better error-per-multiply (Niemitalo's 0.7° at 8 multiplies is the benchmark to beat).
5. **Treat oversampling as a documented alternative, not the main path:** include the s-domain 0.40° limit as the asymptotic argument, but recommend per-rate redesign for runtime efficiency in Faust.

## Curated Bibliography (BibTeX)

```bibtex
@article{Gerzon1985UHJ,
  author  = {Gerzon, Michael A.},
  title   = {Ambisonics in Multichannel Broadcasting and Video},
  journal = {Journal of the Audio Engineering Society},
  volume  = {33}, number = {11}, pages = {859--871}, year = {1985}, month = nov,
  note    = {AES E-Library ID 4419; also AES Preprint 2034, 74th Convention, 1983.
             Canonical UHJ (C-format) encode/decode matrix; the ``j'' 90-degree operator.
             DIRECTLY CITABLE: the primary UHJ source you are building on.}
}

@article{RegaliaMitraVaidyanathan1988,
  author  = {Regalia, Phillip A. and Mitra, Sanjit K. and Vaidyanathan, P. P.},
  title   = {The Digital All-Pass Filter: A Versatile Signal Processing Building Block},
  journal = {Proceedings of the IEEE},
  volume  = {76}, number = {1}, pages = {19--37}, year = {1988}, month = jan,
  doi     = {10.1109/5.3286},
  note    = {Survey listing Hilbert transformation among all-pass applications;
             structural losslessness => robustness to quantization.
             DIRECTLY CITABLE: foundational framing of the all-pass building block.}
}

@article{VaidyanathanRegaliaMitra1987,
  author  = {Vaidyanathan, P. P. and Regalia, Phillip A. and Mitra, Sanjit K.},
  title   = {Design of Doubly-Complementary {IIR} Digital Filters Using a Single
             Complex All-Pass Filter, with Multirate Applications},
  journal = {IEEE Transactions on Circuits and Systems},
  volume  = {34}, number = {4}, pages = {378--389}, year = {1987},
  note    = {Real doubly-complementary pairs from one complex all-pass; the
             theoretical bridge to quadrature pairs. CITABLE for topology lineage.}
}

@article{Ansari1987Hilbert,
  author  = {Ansari, Rashid},
  title   = {{IIR} Discrete-Time Hilbert Transformers},
  journal = {IEEE Transactions on Acoustics, Speech, and Signal Processing},
  volume  = {35}, number = {8}, pages = {1116--1119}, year = {1987},
  note    = {Hilbert transformers from generalized half-band filters; normalized
             (fraction-of-fs) design. DIRECTLY CITABLE: the canonical IIR Hilbert source.}
}

@article{Ansari1985Halfband,
  author  = {Ansari, Rashid},
  title   = {Elliptic Filter Design for a Class of Generalized Halfband Filters},
  journal = {IEEE Transactions on Acoustics, Speech, and Signal Processing},
  volume  = {33}, number = {5}, pages = {1146--1150}, year = {1985},
  note    = {Elliptic half-band prototype underlying the polyphase Hilbert form.
             CITABLE for the half-band derivation.}
}

@article{SchusslerSteffen1998,
  author  = {Sch\"{u}{\ss}ler, H. W. and Steffen, P.},
  title   = {Halfband Filters and Hilbert Transformers},
  journal = {Circuits, Systems and Signal Processing},
  volume  = {17}, number = {2}, pages = {137--164}, year = {1998},
  doi     = {10.1007/BF01202851},
  note    = {Thorough treatment connecting half-band filters to phase-splitting
             all-pass Hilbert transformers; rich reference list. CITABLE survey.}
}

@inproceedings{HarrisBerdahlAbel2010,
  author    = {Harris, Daniel and Berdahl, Edgar and Abel, Jonathan S.},
  title     = {An Infinite Impulse Response ({IIR}) Hilbert Transformer Filter
               Design Technique for Audio},
  booktitle = {Proc. 129th Audio Engineering Society Convention},
  year      = {2010}, month = nov, note = {Paper 8258. San Francisco, CA.
             Half-band -> Hilbert via pole motion + frequency warping, in cascaded
             first-order all-pass form. DIRECTLY CITABLE: the design PROCEDURE you
             apply; their warping step is where fs enters.}
}

@article{Lang1998Allpass,
  author  = {Lang, Markus},
  title   = {Allpass Filter Design and Applications},
  journal = {IEEE Transactions on Signal Processing},
  volume  = {46}, number = {9}, pages = {2505--2514}, year = {1998},
  note    = {Generalized Remez (Chebyshev/minimax) all-pass phase design with
             guaranteed convergence. DIRECTLY CITABLE: basis for the minimax fit.}
}

@article{LangLaakso1994,
  author  = {Lang, Markus and Laakso, Tapio I.},
  title   = {Simple and Robust Method for the Design of Allpass Filters Using
             Least-Squares Phase Error Criterion},
  journal = {IEEE Transactions on Circuits and Systems II},
  volume  = {41}, number = {1}, pages = {40--48}, year = {1994},
  note    = {Least-squares phase-error all-pass design. CITABLE alternative to minimax.}
}

@article{ZhangIwakura1999,
  author  = {Zhang, Xi and Iwakura, Hiroshi},
  title   = {Design of {IIR} Digital Allpass Filters Based on Eigenvalue Problem},
  journal = {IEEE Transactions on Signal Processing},
  volume  = {47}, number = {2}, pages = {554--559}, year = {1999},
  note    = {Equiripple-phase all-pass via eigenvalue iteration; demonstrated on
             Hilbert transformers. CITABLE for equiripple phase guarantees.}
}

@article{Bedrosian1960,
  author  = {Bedrosian, Sam D.},
  title   = {Normalized Design of 90-Degree Phase-Difference Networks},
  journal = {IRE Transactions on Circuit Theory},
  volume  = {7}, number = {2}, pages = {128--136}, year = {1960}, month = jun,
  doi     = {10.1109/TCT.1960.1086659},
  note    = {Analog all-pass phase-difference networks in NORMALIZED (scalable)
             frequency. CITABLE: the etymological/historical root of ``normalized design''.}
}

@article{Weaver1954,
  author  = {Weaver, Donald K.},
  title   = {Design of {RC} Wide-Band 90-Degree Phase-Difference Network},
  journal = {Proceedings of the IRE},
  volume  = {42}, pages = {671--676}, year = {1954}, month = apr,
  note    = {``Cookbook'' analog RC phase-difference network design; cited by later
             digital PSN patents via bilinear transform. CITABLE for lineage.}
}

@article{Saraga1950,
  author  = {Saraga, Wolja},
  title   = {The Design of Wide-Band Phase-Splitting Networks},
  journal = {Proceedings of the IRE},
  volume  = {38}, pages = {754--770}, year = {1950},
  note    = {Foundational analog two-phase (quadrature) network synthesis.
             CITABLE for historical context.}
}

@article{Darlington1950,
  author  = {Darlington, Sidney},
  title   = {Realization of a Constant Phase Difference},
  journal = {Bell System Technical Journal},
  volume  = {29}, number = {1}, pages = {94--104}, year = {1950},
  note    = {Network-theoretic basis for constant-phase-difference all-pass pairs.
             CITABLE for theoretical roots.}
}

@misc{Niemitalo2003,
  author = {Niemitalo, Olli},
  title  = {{90} Degree Phase Difference {IIR} Allpass Pair (Hilbert transform note)},
  year   = {2003}, howpublished = {\url{https://yehar.com/blog/?p=368}},
  note   = {Practitioner polyphase-IIR pair: 8 multiplies, 90 +/- 0.7 deg over
             0.998*Nyquist. CITABLE as the widely-used audio reference design.}
}

@misc{deSorasHIIR,
  author = {de Soras, Laurent},
  title  = {{HIIR}: An Oversampling and Hilbert Transform Library in {C++}},
  howpublished = {\url{https://ldesoras.fr/prod.html}},
  note   = {Two-path polyphase IIR for 2x resampling AND pi/2 phase difference;
            includes coefficient computation. CITABLE: practical polyphase implementation
            and the oversampling/Hilbert duality.}
}

@misc{RBJCookbook,
  author = {Bristow-Johnson, Robert},
  title  = {Cookbook Formulae for Audio {EQ} Biquad Filter Coefficients},
  howpublished = {Audio-EQ-Cookbook; W3C Audio EQ Cookbook reproduction},
  note   = {The all-pass biquad (APF) and bilinear-transform-with-prewarp definitions
            your H_R/H_I cascades use. DIRECTLY CITABLE: the topology and the warping
            mechanism behind the measured drift.}
}

@misc{ATKkernels,
  author = {Anderson, Joseph and {Ambisonic Toolkit Community}},
  title  = {Ambisonic Toolkit Kernels and {ATK} for Reaper ({JSFX})},
  howpublished = {\url{https://github.com/ambisonictoolkit/atk-reaper};
                  \url{https://github.com/ambisonictoolkit/atk-kernels}},
  note   = {UHJ uses a single sample-rate-INDEPENDENT IR kernel (folder ``None'';
            source comment ``IR in UHJ decoding does not depend on sample rate''),
            unlike the per-rate HRTF kernels. DIRECTLY CITABLE contrast case.}
}

@misc{LossiusAnderson2014,
  author = {Lossius, Trond and Anderson, Joseph},
  title  = {{ATK} Reaper: The Ambisonic Toolkit as {JSFX} Plugins},
  booktitle = {Proc. Joint ICMC/SMC Conference}, year = {2014}, address = {Athens},
  note   = {Implementation paper for ATK's Reaper port. CITABLE for the ATK UHJ
            implementation details.}
}

@misc{WigginsWigWare,
  author = {Wiggins, Bruce},
  title  = {{WigWare} {UHJ} Encoder/Decoder/Transcoder ({Reaper} {JSFX})},
  howpublished = {\url{https://www.brucewiggins.co.uk/}},
  note   = {Open UHJ tool using ``allpass filter based phase shift networks ...
            similar to the original hardware,'' based on Gerzon 1985. CITABLE as an
            IIR-all-pass UHJ implementation; coefficient-per-fs handling unverified.}
}
```

## Caveats / Confidence Note

- **Well-established (high confidence):** the all-pass canon citations (Regalia–Mitra–Vaidyanathan, Ansari, Lang, Zhang–Iwakura, Schüßler–Steffen); Gerzon 1985 as the UHJ source (exact volume/pages/ID verified); the bilinear-transform-with-prewarp mechanism (RBJ cookbook, verbatim); ATK's UHJ kernel being sample-rate-independent (verified from the atk-reaper source comment); normalized-frequency design in Bedrosian and Ansari; Niemitalo's 0.7°/8-multiply figure (his own words).
- **Inferred / my own inference (explicitly flagged):** that the researcher's specific (f,Q)-recompute-versus-redesign articulation is absent from the UHJ literature — this is an absence-of-evidence conclusion drawn from ~17 web searches plus a targeted subagent pass, **not** a proof of non-existence; a determined full-text search of the AES e-library and IEEE Xplore could in principle surface a closer precedent.
- **Could not fully verify:** the exact coefficient-handling in Wiggins's WigWare JSFX (source not web-indexed; the standard JSFX idiom recomputes from `srate`, but this is inference); whether any closed commercial UHJ product internally redesigns per fs; the precise phase-error-vs-order curves for each topology at the researcher's exact band edges (these are design-dependent and should be regenerated, not quoted from the literature).
- **Taken as given, not independently reproduced:** the researcher's own measurements (1.36°/22°/47°/54° at 48/44.1/96/192 kHz; 2.04°→0.42° after per-rate fit; the s-domain 0.40° ideal). These are consistent with the bilinear-warping mechanism and are reported as the researcher's empirical findings.
- **A correction to the original premise worth noting in the paper:** the task hypothesised that ATK UHJ uses per-sample-rate FIR tap sets. It does not — ATK's UHJ kernel is sample-rate-independent (one `None` kernel parameterised only by length); the 44.1k–192k kernel families apply to ATK's HRTF/binaural decoders. This strengthens, rather than weakens, the gap argument: the one widely-used open UHJ tool that *could* have addressed quadrature-versus-fs instead sidesteps it by using FIR convolution.