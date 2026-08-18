// SEAM-LTM · strx_export — writes the band table to a file a human reads first.
//
// The measurement leaves strx as plain text, on purpose. It is read by the
// operator before it is acted on, the same way a preset is loaded with the
// amplifier disconnected: nothing here should be trustworthy only because a
// program produced it.
//
// The file carries the CONTEXT and not just the 31 numbers. A folder of
// tables with no sample rate, no source and no position is a folder of
// numbers that cannot be compared — which is exactly what the 2026-08-18
// session log had to admit about the first STONE calibration. `position` and
// `note` are written empty for the operator to fill in: an empty field in a
// file is visible, a fact never recorded is not.
#pragma once

#include "strx_dsp.h"
#include "strx_bands.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

namespace Seam { namespace strx {

struct ExportContext {
    double      sampleRate = 48000.0;
    std::string source     = "no source on the calibration bus";
    int         stoneId    = 0;          // 1..8, or 0 when undeclared
    std::tm     when{};
};

// The nominal ISO 266 names, which are what an operator and an equaliser call
// these bands — the exact centres are in the Hz column beside them.
inline const char* bandName(int i) {
    static const char* kNames[kNumBands] = {
        "20","25","31.5","40","50","63","80","100","125","160","200","250",
        "315","400","500","630","800","1k","1.25k","1.6k","2k","2.5k","3.15k",
        "4k","5k","6.3k","8k","10k","12.5k","16k","20k" };
    return (i >= 0 && i < kNumBands) ? kNames[i] : "?";
}

inline std::string formatBandTable(const AnalysisFrame& fr, const ExportContext& ctx) {
    char stamp[32] = "";
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &ctx.when);

    std::string out;
    out.reserve(4096);
    char line[256];

    out += "# SEAM strx - ISO 266 third-octave band table\n";
    std::snprintf(line, sizeof line, "saved:        %s\n", stamp);              out += line;
    std::snprintf(line, sizeof line, "sample-rate:  %.0f Hz\n", ctx.sampleRate); out += line;
    std::snprintf(line, sizeof line, "source:       %s\n", ctx.source.c_str()); out += line;
    std::snprintf(line, sizeof line, "integration:  %.1f s\n", fr.bandSeconds); out += line;
    std::snprintf(line, sizeof line, "bands:        %d of %d measurable at this sample rate\n",
                  fr.bandCount, kNumBands);                                      out += line;
    out += "\n";
    out += "position:     \n";
    out += "note:         \n";
    out += "\n";
    out += "# comp_db is the correction to dial into the amplifier: positive means RAISE.\n";
    out += "# It is a band-integrated deviation, so it under-reports a narrow defect -\n";
    out += "# a 6 dB dip one third of an octave wide reads about 3.5 dB.\n";
    out += "# level_db is the raw band level. It is meaningless in absolute terms, and\n";
    out += "# it is how you see whether a band had any signal to begin with.\n";
    out += "# settled below 1.00 means the band has not yet had three time constants:\n";
    out += "# ~11 s at 20 Hz, ~0.2 s at 1 kHz. Below 1.00 the number is still moving.\n";
    out += "\n";
    // Two decimals on the centre frequency, not one: the 20 Hz band sits at
    // 19.95 Hz and at one decimal it prints as "20.0", which is the nominal
    // name in the column that exists to give the exact value.
    // No table at all when there is nothing behind it. The first file this
    // export ever wrote (2026-08-18 18:38) said "0 of 31 measurable" in its
    // header and then printed 31 rows of perfect zeros, because the row count
    // fell back to the full band list: a flawless-looking measurement of
    // nothing, which is the one thing a calibration record must never be.
    if (fr.bandCount <= 0 || fr.bandSeconds <= 0.0f) {
        out += "# NO MEASUREMENT. The analyser had not run when this was saved -- no\n";
        out += "# audio through the plugin, or the table had just been reset. There is\n";
        out += "# no band table below because there are no numbers, not because they\n";
        out += "# were zero.\n";
        return out;
    }

    out += "band          hz   comp_db   level_db   settled\n";

    const int n = (fr.bandCount <= kNumBands) ? fr.bandCount : kNumBands;
    for (int i = 0; i < n; ++i) {
        std::snprintf(line, sizeof line, "%-6s %10.2f  %+8.1f   %8.1f      %4.2f\n",
                      bandName(i), bandFc(i), fr.bandComp[i], fr.bandLevel[i],
                      fr.bandSettle[i]);
        out += line;
    }
    if (n < kNumBands) {
        std::snprintf(line, sizeof line,
                      "\n# %d band(s) withheld: their passband runs into Nyquist at this\n"
                      "# sample rate, where the reading describes the rate and not the room.\n",
                      kNumBands - n);
        out += line;
    }
    return out;
}

// Returns true on success. On failure `error` says why in words the operator
// can act on -- most often that the volume is not mounted.
inline bool writeBandTable(const std::string& dir, const AnalysisFrame& fr,
                            const ExportContext& ctx,
                            std::string* pathOut, std::string* error) {
    char stamp[32] = "";
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d-%H%M%S", &ctx.when);
    char name[64];
    if (ctx.stoneId >= 1 && ctx.stoneId <= 8)
        std::snprintf(name, sizeof name, "%s-stone%d.txt", stamp, ctx.stoneId);
    else
        std::snprintf(name, sizeof name, "%s-stone-undeclared.txt", stamp);

    const std::string path = dir + "/" + name;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (error) *error = "cannot write to " + dir + " - volume not mounted?";
        return false;
    }
    const std::string text = formatBandTable(fr, ctx);
    const size_t written = std::fwrite(text.data(), 1, text.size(), f);
    const bool ok = (written == text.size());
    std::fclose(f);
    if (!ok) { if (error) *error = "short write to " + path; return false; }
    if (pathOut) *pathOut = path;
    return true;
}

} } // namespace Seam::strx
