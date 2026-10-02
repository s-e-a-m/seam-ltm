//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · delrm — the footer's text (SDK-free)
//
// D in milliseconds, in samples and the session's rate, the three numbers
// that show the prime changing with the rate. Kept apart from the VSTGUI
// view so that a test can hold it to the view's width: 36 characters of
// InfoFont 12 in 260 px.
//──────────────────────────────────────────────────────────────────────────
#pragma once
#include <cstddef>
#include <cstdio>

namespace delrm {

constexpr std::size_t kFooterChars = 36;

// "D 30.24 ms 2903 samples 96 kHz"; "D — inactive" when fs is 0.
inline void formatDelayLine(char* buf, std::size_t size, unsigned d, double fs) {
    if (fs > 0.0)
        std::snprintf(buf, size, "D %.2f ms %u samples %g kHz", 1000.0 * d / fs, d, fs / 1000.0);
    else
        std::snprintf(buf, size, "D \xE2\x80\x94 inactive");
}

} // namespace delrm
