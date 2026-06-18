#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "hilbert_dsp.h"

#include <cstdio>

namespace Seam {

// Read-only view: prints the live-designed coefficients and the achieved
// quadrature error for the current sample rate and topology. The table adapts
// to the active topology — RBJ shows the (f, Q) sections of each branch,
// polyphase shows the per-path `a` coefficients. The label/value colour split
// and font follow the suite's title/subtitle scheme, resolved from the uidesc
// (TextDim labels, TextLight data, Source Code Pro Light).
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
        const CCoord x0 = r.left + 4;
        CCoord y = r.top;
        char buf[128];
        // Each row: tag (dim) + value (light). The CRect overload vertically
        // centres each line, so the row top is the anchor.
        auto row = [&](const char* tag, const char* value) {
            c->setFontColor(labelColor);
            c->drawString(tag, CRect(x0, y, x0 + tagW, y + rowH), kLeftText);
            c->setFontColor(valueColor);
            c->drawString(value, CRect(x0 + tagW, y, r.right - 2, y + rowH), kLeftText);
            y += rowH;
        };

        double maxErrorDeg = 0.0, sampleRate = dsp->sampleRate();
        bool converged = false;
        if (dsp->topology() == hilbert::Topology::RBJ) {
            const auto& d = dsp->rbjDesign();
            for (int i = 0; i < d.nSections; ++i) {
                std::snprintf(buf, sizeof buf, "%8.2f Hz  Q %.3f", d.hr[i].f, d.hr[i].Q);
                row("HR", buf);
            }
            for (int i = 0; i < d.nSections; ++i) {
                std::snprintf(buf, sizeof buf, "%8.2f Hz  Q %.3f", d.hi[i].f, d.hi[i].Q);
                row("HI", buf);
            }
            maxErrorDeg = d.maxErrorDeg; converged = d.converged;
        } else {
            const auto& d = dsp->polyDesign();
            for (int i = 0; i < d.nA; ++i) {
                std::snprintf(buf, sizeof buf, "a %.9f", d.a[i]);
                row("A", buf);
            }
            for (int i = 0; i < d.nB; ++i) {
                std::snprintf(buf, sizeof buf, "a %.9f", d.a[d.nA + i]);
                row("B", buf);
            }
            maxErrorDeg = d.maxErrorDeg; converged = d.converged;
        }

        // Achieved error as a footer line below a small gap.
        y += 6.0;
        const char* errLabel = "max err ";
        c->setFontColor(labelColor);
        c->drawString(errLabel, CRect(x0, y, r.right - 2, y + rowH), kLeftText);
        const CCoord errLabelW = c->getStringWidth(errLabel);
        std::snprintf(buf, sizeof buf, "%.2f deg @ %.4g kHz%s",
                      maxErrorDeg, sampleRate / 1000.0,
                      converged ? "" : " (fallback)");
        c->setFontColor(valueColor);
        c->drawString(buf, CRect(x0 + errLabelW, y, r.right - 2, y + rowH), kLeftText);
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
