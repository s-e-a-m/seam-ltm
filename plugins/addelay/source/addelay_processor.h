//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · ADDELAY — Distance Air-Absorption Delay
//
// ddelay's exact metres->samples+nextPrime delay (linear-phase bulk
// propagation) plus a minimum-phase air-absorption filter fitted to
// ISO 9613-1 alpha*d (the min-phase residual that completes the model),
// with optional 1/r geometric spreading. 4ch in -> 4ch out; one distance
// drives delay, filter and spreading; the same filter on all four channels
// preserves inter-channel phase by construction.
//
// FAUST REFERENCE (seam.math.lib): isos=331.4; imt2samp; sff.np nextPrime.
// FAUST REFERENCE (seam.filters.lib): the air-absorption filter (roadmap).
// ISO REFERENCE: ISO 9613-1:1993 (alpha), Bass/Sutherland/Zuckerwar behind it.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "addelay_dsp.h"

namespace Seam {

class AddelayProcessor : public Steinberg::Vst::SingleComponentEffect {
public:
    AddelayProcessor();

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new AddelayProcessor);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 s) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* in, Steinberg::int32 numIn,
        Steinberg::Vst::SpeakerArrangement* out, Steinberg::int32 numOut) SMTG_OVERRIDE;

    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;

private:
    // Pull current normalized params, denormalize, and push into the DSP.
    // Guarded by a dirty-check so identical automation points don't re-fit.
    void applyParams();

    addelay::AirDelay dsp_;

    // Cached denormalized inputs for the dirty-check.
    double lastD_ = -1.0, lastT_ = -999.0, lastRh_ = -1.0;
    int    lastTopo_ = -1, lastSpread_ = -1;
};

} // namespace Seam
