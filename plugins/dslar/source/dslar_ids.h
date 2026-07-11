#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Seam {

// FUID — 14th plugin in the SEAM-LTM suite.
// Pattern: 0x5E4D + sequential index (ltburst=000C, ltglide=000D, dslar=000E).
static const Steinberg::FUID DSLARProcessorUID(
    0x5E4D000E, 0xB2C3D4E5, 0x44534C41, 0x52000000);   // "DSLA","R\0\0\0"

enum DSLARParams : Steinberg::Vst::ParamID {
    kParamPower       = 100,   // 0/1  system on/off (2000 ms anti-click fade)
    kParamDrive       = 101,   // 1..4 audio pre-gain
    kParamTarget      = 102,   // 0..1 homeostat reference
    kParamSteepness   = 103,   // 1..80 homeostat exponent
    kParamSmoothing   = 104,   // 1..1000 ms control smoothing
    kParamLoopDelay   = 105,   // 1..200 ms feedforward loop delay (tab1)
    kParamDecorr      = 106,   // 1..200 ms decorrelation tap (tab2)
    kParamOutput      = 107,   // 0..1 final VCA
    // Read-only display parameters, pushed from the audio thread:
    kParamMeterR      = 200,   // Hann RMS r, normalized [0,1] (floor -60 dBFS)
    kParamMeterG      = 201,   // loop gain g, normalized [0,1] (floor -60 dBFS)
};

static constexpr double kDriveMin = 1.0,   kDriveMax = 4.0,    kDriveDef = 1.0;
static constexpr double kTargetMin = 0.0,  kTargetMax = 1.0,   kTargetDef = 1.0;
static constexpr double kSteepMin = 1.0,   kSteepMax = 80.0,   kSteepDef = 40.0;
static constexpr double kSmoothMin = 1.0,  kSmoothMax = 1000.0, kSmoothDef = 200.0;
static constexpr double kTab1Min = 1.0,    kTab1Max = 200.0,   kTab1Def = 50.0;
static constexpr double kTab2Min = 1.0,    kTab2Max = 200.0,   kTab2Def = 20.0;
static constexpr double kOutMin = 0.0,     kOutMax = 1.0,      kOutDef = 1.0;
static constexpr double kMeterFloorDb = -60.0;

} // namespace Seam
