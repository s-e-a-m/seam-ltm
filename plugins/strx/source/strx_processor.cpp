#include "strx_processor.h"
#include "strx_ids.h"
#include "strx_meters.h"
#include "strx_goniometer.h"
#include "strx_spectrum.h"
#include "strx_status.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "pluginterfaces/base/ibstream.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <algorithm>
#include <string>
#include <type_traits>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Seam {

StrxProcessor::StrxProcessor() {}

tresult PLUGIN_API StrxProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    addAudioInput (STR16("Input"),  SpeakerArr::kStereo);
    addAudioOutput(STR16("Output"), SpeakerArr::kStereo);

    // Observation analyzer only — no automatable parameters, no state.
    return kResultOk;
}

tresult PLUGIN_API StrxProcessor::terminate() { return SingleComponentEffect::terminate(); }

tresult PLUGIN_API StrxProcessor::setActive(TBool state) {
    if (state) analyzer_.reset();
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API StrxProcessor::setupProcessing(ProcessSetup& setup) {
    sampleRate_ = setup.sampleRate;
    analyzer_.prepare(setup.sampleRate);
    const size_t maxBlock = (size_t) std::max<int32>(setup.maxSamplesPerBlock, 0);
    convL_.resize(maxBlock);
    convR_.resize(maxBlock);
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API StrxProcessor::process(ProcessData& data) {
    if (data.numInputs == 0 || data.numOutputs == 0 || data.numSamples == 0)
        return kResultOk;

    void** in  = getChannelBuffersPointer(processSetup, data.inputs[0]);
    void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);

    // Apply what the GUI read from the bus. Once per block, never per sample,
    // and never by touching the bus from this thread.
    analyzer_.setGlideMode(specGlide_.load(std::memory_order_relaxed));
    const uint32_t he = holdEpoch_.load(std::memory_order_relaxed);
    if (he != lastHoldEpoch_) { lastHoldEpoch_ = he; analyzer_.resetHold(); }

    if (data.symbolicSampleSize == kSample32)
        processBlock<float>((float**)in, (float**)out, data.numSamples);
    else
        processBlock<double>((double**)in, (double**)out, data.numSamples);
    return kResultOk;
}

tresult PLUGIN_API StrxProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API StrxProcessor::setBusArrangements(
    SpeakerArrangement* ins, int32 numIns, SpeakerArrangement* outs, int32 numOuts) {
    if (numIns != 1 || numOuts != 1) return kResultFalse;
    if (SpeakerArr::getChannelCount(ins[0]) != 2) return kResultFalse;
    if (SpeakerArr::getChannelCount(outs[0]) != 2) return kResultFalse;
    return SingleComponentEffect::setBusArrangements(ins, numIns, outs, numOuts);
}

tresult PLUGIN_API StrxProcessor::setState(IBStream* state) {
    // No parameters to restore — observation-only plugin.
    return state ? kResultOk : kResultFalse;
}

tresult PLUGIN_API StrxProcessor::getState(IBStream* state) {
    // No parameters to persist — observation-only plugin.
    return state ? kResultOk : kResultFalse;
}

IPlugView* PLUGIN_API StrxProcessor::createView(FIDString name) {
    if (FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "editor", "strx.uidesc");
    return nullptr;
}

VSTGUI::CView* PLUGIN_API StrxProcessor::createCustomView(
    VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& /*attributes*/,
    const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor* /*editor*/) {
    if (name && std::string(name) == kViewMeters) {
        VSTGUI::CFontRef font = description ? description->getFont("InfoFont") : nullptr;
        VSTGUI::CColor structure = VSTGUI::kGreyCColor, text = VSTGUI::kWhiteCColor;
        VSTGUI::CColor track = VSTGUI::kBlackCColor, fill(0xc8, 0xa2, 0x4a, 0xff);
        VSTGUI::CColor inv(0xc0, 0x40, 0x40, 0xff);
        if (description) {
            description->getColor("Structure", structure);
            description->getColor("TextLight", text);
            description->getColor("SliderTrack", track);
            description->getColor("MeterFill", fill);
            description->getColor("MeterInv", inv);
        }
        return new Seam::StrxMeters(VSTGUI::CRect(0, 0, 270, 300), this, font,
                                     structure, text, track, fill, inv);
    }
    if (name && std::string(name) == kViewGoniometer) {
        VSTGUI::CFontRef font = description ? description->getFont("InfoFont") : nullptr;
        VSTGUI::CColor structure = VSTGUI::kGreyCColor, text = VSTGUI::kWhiteCColor;
        VSTGUI::CColor track = VSTGUI::kBlackCColor, fill(0xc8, 0xa2, 0x4a, 0xff);
        VSTGUI::CColor needle(0x4a, 0x9e, 0xc8, 0xff);   // SliderActive azure (the M colour)
        if (description) {
            description->getColor("Structure", structure);
            description->getColor("TextLight", text);
            description->getColor("SliderTrack", track);
            description->getColor("MeterFill", fill);
            description->getColor("SliderActive", needle);
        }
        return new Seam::StrxGoniometer(VSTGUI::CRect(0, 0, 260, 300), this, font,
                                         structure, text, track, fill, needle);
    }
    if (name && std::string(name) == kViewSpectrum) {
        VSTGUI::CFontRef font = description ? description->getFont("InfoFont") : nullptr;
        VSTGUI::CColor structure = VSTGUI::kGreyCColor, text = VSTGUI::kWhiteCColor;
        VSTGUI::CColor track = VSTGUI::kBlackCColor;
        VSTGUI::CColor colorM(0x4a, 0x9e, 0xc8, 0xff), colorS(0xc8, 0xa2, 0x4a, 0xff);
        if (description) {
            description->getColor("Structure", structure);
            description->getColor("TextLight", text);
            description->getColor("SliderTrack", track);
            description->getColor("SliderActive", colorM);
            description->getColor("MeterFill", colorS);
        }
        return new Seam::StrxSpectrum(VSTGUI::CRect(0, 0, 560, 240), this, font,
                                       structure, text, track, colorM, colorS);
    }
    if (name && std::string(name) == kViewStatus) {
        VSTGUI::CFontRef font = description ? description->getFont("InfoFont") : nullptr;
        VSTGUI::CColor text = VSTGUI::kWhiteCColor;
        if (description) description->getColor("TextLight", text);
        return new Seam::StrxStatusLine(VSTGUI::CRect(0, 0, 560, 26), this, font, text);
    }
    return nullptr;
}

template <typename SampleType>
void StrxProcessor::processBlock(SampleType** in, SampleType** out, int numSamples) {
    // Pass-through: audio is analysis-only, never altered.
    if (in[0] != out[0]) std::copy(in[0], in[0] + numSamples, out[0]);
    if (in[1] != out[1]) std::copy(in[1], in[1] + numSamples, out[1]);

    if constexpr (std::is_same_v<SampleType, float>) {
        analyzer_.process(in[0], in[1], numSamples);
    } else {
        // kSample64: Analyzer::process() takes float buffers, so convert into
        // the pre-sized scratch buffers (no audio-thread allocation).
        for (int i = 0; i < numSamples; ++i) {
            convL_[(size_t) i] = (float) in[0][i];
            convR_[(size_t) i] = (float) in[1][i];
        }
        analyzer_.process(convL_.data(), convR_.data(), numSamples);
    }
}

template void StrxProcessor::processBlock<float>(float**, float**, int);
template void StrxProcessor::processBlock<double>(double**, double**, int);

} // namespace Seam

// ----- Factory -----
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(INLINE_UID_FROM_FUID(Seam::StrxProcessorUID),
               PClassInfo::kManyInstances, kVstAudioEffectClass,
               "SEAM STRX", Vst::kDistributable,
               "Fx|Analyzer", FULL_VERSION_STR, kVstVersionString,
               Seam::StrxProcessor::createInstance)
END_FACTORY
