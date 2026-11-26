#include "lessness_processor.h"
#include "lessness_ids.h"
#include "lessness_panes.h"
#include "lessness_buttons.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "pluginterfaces/base/ibstream.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <algorithm>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Seam {

tresult PLUGIN_API LessnessProcessor::initialize(FUnknown* context) {
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk) return r;

    // Stereo by default; setBusArrangements takes any equal width.
    addAudioInput (STR16("Input"),  SpeakerArr::kStereo);
    addAudioOutput(STR16("Output"), SpeakerArr::kStereo);

    // No parameters, no state, and no hub registration: see the header.
    return kResultOk;
}

tresult PLUGIN_API LessnessProcessor::process(ProcessData& data) {
    // Nothing here reads or writes the hub: the audio path is a copy.
    if (data.numInputs == 0 || data.numOutputs == 0 || data.numSamples <= 0)
        return kResultOk;

    AudioBusBuffers& in  = data.inputs[0];
    AudioBusBuffers& out = data.outputs[0];
    const int32 channels = std::min(in.numChannels, out.numChannels);

    void** pin  = getChannelBuffersPointer(processSetup, in);
    void** pout = getChannelBuffersPointer(processSetup, out);
    if (data.symbolicSampleSize == kSample32)
        lessness::passThrough((float**)pin, (float**)pout, channels, data.numSamples);
    else
        lessness::passThrough((double**)pin, (double**)pout, channels, data.numSamples);

    out.silenceFlags = in.silenceFlags;
    return kResultOk;
}

tresult PLUGIN_API LessnessProcessor::canProcessSampleSize(int32 s) {
    return (s == kSample32 || s == kSample64) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API LessnessProcessor::setBusArrangements(
    SpeakerArrangement* ins, int32 numIns, SpeakerArrangement* outs, int32 numOuts) {
    if (numIns != 1 || numOuts != 1 || !ins || !outs) return kResultFalse;
    if (!lessness::acceptsChannels(SpeakerArr::getChannelCount(ins[0]),
                                   SpeakerArr::getChannelCount(outs[0])))
        return kResultFalse;
    return SingleComponentEffect::setBusArrangements(ins, numIns, outs, numOuts);
}

tresult PLUGIN_API LessnessProcessor::setState(IBStream* state) {
    // Nothing to restore: the sheet's folder lives in the hub's conf, not
    // in the host's project.
    return state ? kResultOk : kResultFalse;
}

tresult PLUGIN_API LessnessProcessor::getState(IBStream* state) {
    return state ? kResultOk : kResultFalse;
}

IPlugView* PLUGIN_API LessnessProcessor::createView(FIDString name) {
    if (FIDStringsEqual(name, ViewType::kEditor))
        return new VSTGUI::VST3Editor(this, "editor", "lessness.uidesc");
    return nullptr;
}

VSTGUI::CView* PLUGIN_API LessnessProcessor::createCustomView(
    VSTGUI::UTF8StringPtr name, const VSTGUI::UIAttributes& /*attributes*/,
    const VSTGUI::IUIDescription* description, VSTGUI::VST3Editor* /*editor*/) {
    if (!name) return nullptr;
    const std::string view(name);

    // Pane text is InfoFont (Source Code Pro Light 11), white; frames are
    // Structure; the footer's alarm square is MeterInv.
    VSTGUI::CFontRef font = description ? description->getFont("InfoFont") : nullptr;
    VSTGUI::CColor text = VSTGUI::kWhiteCColor, structure = VSTGUI::kGreyCColor;
    VSTGUI::CColor alarm(0xc0, 0x40, 0x40, 0xff);
    if (description) {
        description->getColor("TextLight", text);
        description->getColor("Structure", structure);
        description->getColor("MeterInv", alarm);
    }

    // Sizes match resource/lessness.uidesc; the description resizes the view
    // to its own origin and size after creation in any case.
    if (view == kViewMembers)
        return new LessnessMembers(VSTGUI::CRect(0, 0, 600, 112), font, text);
    if (view == kViewProposals)
        return new LessnessProposals(VSTGUI::CRect(0, 0, 600, 112), font, text);
    if (view == kViewLines)
        return new LessnessLines(VSTGUI::CRect(0, 0, 600, 84), font, text);
    if (view == kViewSheet)
        return new LessnessSheet(VSTGUI::CRect(0, 0, 600, 18), font, text, alarm, nextFolder_);
    if (view == kViewFolder)
        return new LessnessFolderButton(VSTGUI::CRect(0, 0, 120, 22), font, structure, text,
                                        nextFolder_);
    if (view == kViewNewSheet)
        return new LessnessNewSheetButton(VSTGUI::CRect(0, 0, 120, 22), font, structure, text);
    return nullptr;
}

} // namespace Seam

// ----- Factory -----
BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
    DEF_CLASS2(INLINE_UID_FROM_FUID(Seam::LessnessProcessorUID),
               PClassInfo::kManyInstances, kVstAudioEffectClass,
               "SEAM LESSNESS", Vst::kDistributable,
               "Fx|Analyzer", FULL_VERSION_STR, kVstVersionString,
               Seam::LessnessProcessor::createInstance)
END_FACTORY
