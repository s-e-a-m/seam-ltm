#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "hilbert_dsp.h"

#include <algorithm>
#include <cstdio>

namespace Seam {

// Read-only view: prints the live-designed coefficients and the achieved
// quadrature error for the current sample rate and topology. The table adapts
// to the active topology — RBJ shows the (f, Q) sections of each branch,
// polyphase shows the per-path `a` coefficients. The label/value colour split
// and font follow the suite's title/subtitle scheme, resolved from the uidesc
// (all-white TextLight text, Source Code Pro Light).
class HilbertReadoutView : public VSTGUI::CView {
public:
    HilbertReadoutView(const VSTGUI::CRect& size, const hilbert::HilbertTransformer* dsp,
                       VSTGUI::CFontRef font,
                       const VSTGUI::CColor& labelColor,
                       const VSTGUI::CColor& valueColor)
        : VSTGUI::CView(size), dsp(dsp), font(font),
          labelColor(labelColor), valueColor(valueColor) {
        if (font) font->remember();
        // Poll on the UI thread so a hot sample-rate or topology change (applied
        // from the audio thread) refreshes the readout without reopening.
        setWantsIdle(true);
    }

    ~HilbertReadoutView() override { if (font) font->forget(); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        CView::draw(c);
        if (!dsp) return;
        shownSig = signature(); // mark what this paint reflects
        if (font) c->setFont(font);
        const CRect r = getViewSize();
        const CCoord rowH = 12.0;
        const CCoord tagW = 22.0; // narrow label column for the row table

        // Collect the rows first (tag + value), so the whole block can be
        // centred horizontally instead of hugging the left edge.
        char tags[8][4];
        char vals[8][64];
        int n = 0;
        double maxErrorDeg = 0.0, sampleRate = dsp->sampleRate();
        bool converged = false;
        if (dsp->topology() == hilbert::Topology::RBJ) {
            const auto& d = dsp->rbjDesign();
            for (int i = 0; i < d.nSections && n < 8; ++i, ++n) {
                std::snprintf(tags[n], 4, "HR");
                std::snprintf(vals[n], 64, "%8.2f Hz  Q %.3f", d.hr[i].f, d.hr[i].Q);
            }
            for (int i = 0; i < d.nSections && n < 8; ++i, ++n) {
                std::snprintf(tags[n], 4, "HI");
                std::snprintf(vals[n], 64, "%8.2f Hz  Q %.3f", d.hi[i].f, d.hi[i].Q);
            }
            maxErrorDeg = d.maxErrorDeg; converged = d.converged;
        } else {
            const auto& d = dsp->polyDesign();
            for (int i = 0; i < d.nA && n < 8; ++i, ++n) {
                std::snprintf(tags[n], 4, "A");
                std::snprintf(vals[n], 64, "a %.9f", d.a[i]);
            }
            for (int i = 0; i < d.nB && n < 8; ++i, ++n) {
                std::snprintf(tags[n], 4, "B");
                std::snprintf(vals[n], 64, "a %.9f", d.a[d.nA + i]);
            }
            maxErrorDeg = d.maxErrorDeg; converged = d.converged;
        }

        // The widest value fixes the block width; centre the tag+value block.
        CCoord maxValW = 0.0;
        for (int i = 0; i < n; ++i)
            maxValW = std::max(maxValW, c->getStringWidth(vals[i]));
        const CCoord blockW = tagW + maxValW;
        const CCoord x0 = r.left + std::max(0.0, (r.getWidth() - blockW) / 2.0);

        // Each row: tag (dim) + value (light).
        CCoord y = r.top;
        for (int i = 0; i < n; ++i) {
            c->setFontColor(labelColor);
            c->drawString(tags[i], CRect(x0, y, x0 + tagW, y + rowH), kLeftText);
            c->setFontColor(valueColor);
            c->drawString(vals[i], CRect(x0 + tagW, y, x0 + blockW, y + rowH), kLeftText);
            y += rowH;
        }

        // Achieved error as a footer line below a small gap, centred as a whole.
        y += 6.0;
        const char* errLabel = "max err ";
        char errVal[80];
        std::snprintf(errVal, sizeof errVal, "%.2f deg @ %.4g kHz%s",
                      maxErrorDeg, sampleRate / 1000.0,
                      converged ? "" : " (fallback)");
        const CCoord errLabelW = c->getStringWidth(errLabel);
        const CCoord footW = errLabelW + c->getStringWidth(errVal);
        const CCoord fx = r.left + std::max(0.0, (r.getWidth() - footW) / 2.0);
        c->setFontColor(labelColor);
        c->drawString(errLabel, CRect(fx, y, fx + errLabelW, y + rowH), kLeftText);
        c->setFontColor(valueColor);
        c->drawString(errVal, CRect(fx + errLabelW, y, fx + footW, y + rowH), kLeftText);
        setDirty(false);
    }

    void onIdle() override {
        if (dsp && signature() != shownSig) invalid();
    }

private:
    // Small state signature distinguishing a paint-worthy change: a hot
    // sample-rate change or a topology switch both move this value.
    double signature() const {
        return dsp->sampleRate() * 4.0 +
               (dsp->topology() == hilbert::Topology::Polyphase ? 1.0 : 0.0);
    }

    const hilbert::HilbertTransformer* dsp;
    VSTGUI::CFontRef font;
    VSTGUI::CColor labelColor;
    VSTGUI::CColor valueColor;
    double shownSig = -1.0; // signature the current paint reflects
};

} // namespace Seam
