//─────────────────────────────────────────────────────────────────────────────
// SEAM-LTM · DDELAY — Implementation
//─────────────────────────────────────────────────────────────────────────────

#include "ddelay_processor.h"
#include "ddelay_ids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioprocessoralgo.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include "pluginterfaces/base/ibstream.h"

#include <cstring>
#include <cmath>
#include <algorithm>

namespace Seam {

using namespace Steinberg;
using namespace Steinberg::Vst;

//─────────────────────────────────────────────────────────────────────────────
// Prime helpers
//
// Translation of seam.math.lib's foreign function `next_pr` (h/nextprime.h),
// rewritten as iterative C++ with the original edge-case bugs fixed:
//   - is_prime(1) and is_prime(0) now correctly return false.
//   - The recursive descent on even numbers is replaced by a single
//     parity adjustment and a forward search by 2.
//─────────────────────────────────────────────────────────────────────────────

static bool isPrime (int n)
{
    if (n < 2)             return false;
    if (n < 4)             return true;     // 2, 3
    if ((n & 1) == 0)      return false;    // even > 2
    int lim = static_cast<int>(std::sqrt(static_cast<double>(n)));
    for (int i = 3; i <= lim; i += 2)
        if (n % i == 0) return false;
    return true;
}

// Smallest prime strictly greater than n (matches the semantics of
// seam.math.lib::next_pr: nextPrime(5) = 7, nextPrime(4) = 5).
static int nextPrime (int n)
{
    if (n < 2) return 2;
    int c = (n & 1) ? n + 2 : n + 1;
    while (!isPrime (c)) c += 2;
    return c;
}

//─────────────────────────────────────────────────────────────────────────────
// Construction
//─────────────────────────────────────────────────────────────────────────────

DDELAYProcessor::DDELAYProcessor ()
{
    for (int ch = 0; ch < kNumChannels; ++ch)
    {
        buffer32_[ch].assign (kBufferSize, 0.0f);
        buffer64_[ch].assign (kBufferSize, 0.0);
    }
}

//─────────────────────────────────────────────────────────────────────────────
// Initialization — declare buses and the single shared Distance parameter
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API DDELAYProcessor::initialize (FUnknown* context)
{
    tresult result = SingleComponentEffect::initialize (context);
    if (result != kResultOk)
        return result;

    // 4-channel in, 4-channel out (AmbiX-shaped to match the rest of
    // the suite; the host routes channels regardless of label).
    addAudioInput  (STR16 ("Quad In"),  SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput (STR16 ("Quad Out"), SpeakerArr::kAmbi1stOrderACN);

    // Distance: 0–30 m, default 0 m. Step ~1 cm.
    // Continuous parameter (stepCount = 0) so the display retains the
    // decimal point. The actual delay value is quantized to the nearest
    // millimetre internally — see updateDelaySamples ().
    // setPrecision fixes the host's value readout to millimetre resolution
    // (matching the mm quantization below); Vst::Parameter defaults to 4 decimals.
    auto* dist = new RangeParameter (STR16 ("Distance"), kParamDistance, STR16 ("m"),
                                     0.0, kMaxDistance, 0.0, 0,
                                     ParameterInfo::kCanAutomate);
    dist->setPrecision (3);                     // metres to the millimetre
    parameters.addParameter (dist);

    return kResultOk;
}

tresult PLUGIN_API DDELAYProcessor::terminate ()
{
    return SingleComponentEffect::terminate ();
}

//─────────────────────────────────────────────────────────────────────────────
// Activation — clear ring buffers
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API DDELAYProcessor::setActive (TBool state)
{
    if (state)
    {
        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            std::fill (buffer32_[ch].begin (), buffer32_[ch].end (), 0.0f);
            std::fill (buffer64_[ch].begin (), buffer64_[ch].end (), 0.0);
        }
        writeIndex_ = 0;
        updateDelaySamples ();
    }
    return SingleComponentEffect::setActive (state);
}

tresult PLUGIN_API DDELAYProcessor::setupProcessing (ProcessSetup& setup)
{
    tresult res = SingleComponentEffect::setupProcessing (setup);
    updateDelaySamples ();
    return res;
}

//─────────────────────────────────────────────────────────────────────────────
// Distance → samples → nextprime
//
//   samples = round(distance * SR / 331.4)
//   delay   = nextprime(samples)
//
// The value is clamped so that the read pointer never wraps past the
// write pointer (kBufferSize is a power of two, so a simple mask
// addresses any positive integer offset modulo the buffer).
//─────────────────────────────────────────────────────────────────────────────

void DDELAYProcessor::updateDelaySamples ()
{
    const double sr = processSetup.sampleRate > 0.0 ? processSetup.sampleRate : 48000.0;
    // Quantize the input distance to the nearest millimetre. The slider
    // moves continuously and the display keeps three decimals, but the
    // engine only ever sees mm-rounded values — matching the precision
    // of a hand-held laser distance meter.
    const double mm = std::round (distanceMeters_ * 1000.0) / 1000.0;
    const double rawSamples = mm * sr / kSpeedOfSound;
    int n = static_cast<int>(std::lround (rawSamples));

    int d = (n < 2) ? n : nextPrime (n);

    // Headroom safety: keep at least one sample below buffer size
    // so the prime overshoot can never collide with the write head.
    if (d > kBufferSize - 1) d = kBufferSize - 1;
    if (d < 0)               d = 0;

    delaySamples_.store (d, std::memory_order_relaxed);
}

//─────────────────────────────────────────────────────────────────────────────
// Audio processing
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API DDELAYProcessor::process (ProcessData& data)
{
    //--- Read parameter changes ---
    if (data.inputParameterChanges)
    {
        int32 numChanges = data.inputParameterChanges->getParameterCount ();
        for (int32 i = 0; i < numChanges; ++i)
        {
            IParamValueQueue* queue = data.inputParameterChanges->getParameterData (i);
            if (!queue) continue;

            if (queue->getParameterId () != kParamDistance) continue;

            int32 numPoints = queue->getPointCount ();
            ParamValue value;
            int32 sampleOffset;
            if (queue->getPoint (numPoints - 1, sampleOffset, value) == kResultOk)
            {
                distanceMeters_ = value * kMaxDistance;
                updateDelaySamples ();
            }
        }
    }

    if (data.numInputs == 0 || data.numOutputs == 0)
        return kResultOk;

    int32 inChannels  = data.inputs[0].numChannels;
    int32 outChannels = data.outputs[0].numChannels;
    uint32 sampleFramesSize = getSampleFramesSizeInBytes (processSetup, data.numSamples);
    void** in  = getChannelBuffersPointer (processSetup, data.inputs[0]);
    void** out = getChannelBuffersPointer (processSetup, data.outputs[0]);

    if (data.inputs[0].silenceFlags == getChannelMask (data.inputs[0].numChannels)
        && delaySamples_.load (std::memory_order_relaxed) == 0)
    {
        // Pure passthrough silence
        data.outputs[0].silenceFlags = getChannelMask (outChannels);
        for (int32 i = 0; i < outChannels; ++i)
            memset (out[i], 0, sampleFramesSize);
        return kResultOk;
    }

    data.outputs[0].silenceFlags = 0;

    if (inChannels < kNumChannels || outChannels < kNumChannels)
    {
        for (int32 ch = 0; ch < outChannels; ++ch)
            memset (out[ch], 0, sampleFramesSize);
        return kResultOk;
    }

    if (data.symbolicSampleSize == kSample32)
        processDelay<Sample32> (reinterpret_cast<Sample32**>(in),
                                reinterpret_cast<Sample32**>(out),
                                buffer32_, data.numSamples);
    else
        processDelay<Sample64> (reinterpret_cast<Sample64**>(in),
                                reinterpret_cast<Sample64**>(out),
                                buffer64_, data.numSamples);

    return kResultOk;
}

//─────────────────────────────────────────────────────────────────────────────
// DSP core — pure integer-sample ring-buffer delay
//
// One write index shared by all four channels (channels are
// synchronous within the instance). The delay length is read once per
// block: parameter changes are applied at block boundaries, accepting
// the documented click on change.
//─────────────────────────────────────────────────────────────────────────────

template <typename SampleType>
void DDELAYProcessor::processDelay (SampleType** in, SampleType** out,
                                   std::vector<SampleType> (&buf)[kNumChannels],
                                   int32 numSamples)
{
    const int delay = delaySamples_.load (std::memory_order_relaxed);
    int w = writeIndex_;

    for (int32 i = 0; i < numSamples; ++i)
    {
        const int r = (w - delay) & kBufferMask;
        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            buf[ch][w]  = in[ch][i];
            out[ch][i]  = buf[ch][r];
        }
        w = (w + 1) & kBufferMask;
    }

    writeIndex_ = w;
}

//─────────────────────────────────────────────────────────────────────────────
// State
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API DDELAYProcessor::setState (IBStream* state)
{
    if (!state) return kResultFalse;

    double saved = 0.0;
    if (state->read (&saved, sizeof (saved)) != kResultOk)
        return kResultFalse;

    distanceMeters_ = saved * kMaxDistance;
    updateDelaySamples ();

    if (auto* p = parameters.getParameter (kParamDistance))
        p->setNormalized (saved);

    return kResultOk;
}

tresult PLUGIN_API DDELAYProcessor::getState (IBStream* state)
{
    if (!state) return kResultFalse;

    double saved = parameters.getParameter (kParamDistance)
                 ? parameters.getParameter (kParamDistance)->getNormalized ()
                 : 0.0;
    state->write (&saved, sizeof (saved));
    return kResultOk;
}

//─────────────────────────────────────────────────────────────────────────────
// Bus arrangement — 4 in / 4 out
//─────────────────────────────────────────────────────────────────────────────

tresult PLUGIN_API DDELAYProcessor::setBusArrangements (
    SpeakerArrangement* inputs, int32 numIns,
    SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 1 && numOuts == 1 &&
        SpeakerArr::getChannelCount (inputs[0])  == 4 &&
        SpeakerArr::getChannelCount (outputs[0]) == 4)
    {
        return SingleComponentEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
    }
    return kResultFalse;
}

tresult PLUGIN_API DDELAYProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
    if (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64)
        return kResultTrue;
    return kResultFalse;
}

//─────────────────────────────────────────────────────────────────────────────
// GUI
//─────────────────────────────────────────────────────────────────────────────

IPlugView* PLUGIN_API DDELAYProcessor::createView (FIDString name)
{
    if (name && FIDStringsEqual (name, ViewType::kEditor))
        return new VSTGUI::VST3Editor (this, "view", "ddelay.uidesc");
    return nullptr;
}

} // namespace Seam

//─────────────────────────────────────────────────────────────────────────────
// Plugin factory
//─────────────────────────────────────────────────────────────────────────────

BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

    DEF_CLASS2 (
        INLINE_UID_FROM_FUID (Seam::DDELAYProcessorUID),
        PClassInfo::kManyInstances,
        kVstAudioEffectClass,
        "SEAM DDELAY",
        0,
        "Fx|Delay",
        FULL_VERSION_STR,
        kVstVersionString,
        Seam::DDELAYProcessor::createInstance)

END_FACTORY
