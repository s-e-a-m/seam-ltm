#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "x2uhj_dsp.h"

#include <algorithm>
#include <cstdio>

namespace Seam {

// Read-only view: prints the live-designed (fc, Q) pairs and the achieved
// quadrature error for the current sample rate. The label/value colour split
// and font follow the suite's title/subtitle scheme, resolved from the uidesc
// (all-white TextLight text, Source Code Pro Light).
class QuadratureReadoutView : public VSTGUI::CView {
public:
    QuadratureReadoutView(const VSTGUI::CRect& size, const x2uhj::UHJEncoder* enc,
                          VSTGUI::CFontRef font,
                          const VSTGUI::CColor& labelColor,
                          const VSTGUI::CColor& valueColor)
        : VSTGUI::CView(size), encoder(enc), font(font),
          labelColor(labelColor), valueColor(valueColor) {
        if (font) font->remember();
        // Poll on the UI thread so a hot sample-rate change (which re-runs the
        // encoder design from the audio thread) refreshes the readout without
        // reopening the editor.
        setWantsIdle(true);
    }

    ~QuadratureReadoutView() override { if (font) font->forget(); }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        CView::draw(c);
        if (!encoder) return;
        const auto& d = encoder->design();
        shownSampleRate = d.sampleRate; // mark what this paint reflects
        if (font) c->setFont(font);
        const CRect r = getViewSize();
        const CCoord rowH = 12.0;
        const CCoord tagW = 22.0; // narrow label column for the HR/HI table

        // Collect the rows first (tag + value), so the whole block can be
        // centred horizontally instead of hugging the left edge.
        char tags[16][4];
        char vals[16][64];
        int n = 0;
        for (int i = 0; i < d.nSections && n < 16; ++i, ++n) {
            std::snprintf(tags[n], 4, "HR");
            std::snprintf(vals[n], 64, "%8.2f Hz  Q %.3f", d.hr[i].f, d.hr[i].Q);
        }
        for (int i = 0; i < d.nSections && n < 16; ++i, ++n) {
            std::snprintf(tags[n], 4, "HI");
            std::snprintf(vals[n], 64, "%8.2f Hz  Q %.3f", d.hi[i].f, d.hi[i].Q);
        }

        // The widest value fixes the block width; centre the tag+value block.
        CCoord maxValW = 0.0;
        for (int i = 0; i < n; ++i)
            maxValW = std::max(maxValW, c->getStringWidth(vals[i]));
        const CCoord blockW = tagW + maxValW;
        const CCoord x0 = r.left + std::max(0.0, (r.getWidth() - blockW) / 2.0);

        // Each row: tag (dim) + value (light). The CRect overload vertically
        // centres each line, so the row top is the anchor.
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
                      d.maxErrorDeg, d.sampleRate / 1000.0,
                      d.converged ? "" : " (fallback)");
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
        if (encoder && encoder->design().sampleRate != shownSampleRate)
            invalid();
    }

private:
    const x2uhj::UHJEncoder* encoder;
    VSTGUI::CFontRef font;
    VSTGUI::CColor labelColor;
    VSTGUI::CColor valueColor;
    double shownSampleRate = -1.0; // fs the current paint reflects
};

} // namespace Seam
