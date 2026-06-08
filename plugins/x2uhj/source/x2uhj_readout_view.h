#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "x2uhj_dsp.h"

#include <cstdio>

namespace Seam {

// Read-only view: prints the live-designed (fc, Q) pairs and the achieved
// quadrature error for the current sample rate. The label/value colour split
// and font follow the suite's title/subtitle scheme, resolved from the uidesc
// (TextDim labels, TextLight data, Source Code Pro Light).
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
        const CCoord x0 = r.left + 4;
        CCoord y = r.top;
        char buf[128];
        // Coefficient table: tag (dim) + value (light). The CRect overload
        // vertically centres each line, so the row top is the anchor (the CPoint
        // overload would treat y as the text baseline).
        auto row = [&](const char* tag, const char* value) {
            c->setFontColor(labelColor);
            c->drawString(tag, CRect(x0, y, x0 + tagW, y + rowH), kLeftText);
            c->setFontColor(valueColor);
            c->drawString(value, CRect(x0 + tagW, y, r.right - 2, y + rowH), kLeftText);
            y += rowH;
        };
        for (int i = 0; i < d.nSections; ++i) {
            std::snprintf(buf, sizeof buf, "%8.2f Hz  Q %.3f", d.hr[i].f, d.hr[i].Q);
            row("HR", buf);
        }
        for (int i = 0; i < d.nSections; ++i) {
            std::snprintf(buf, sizeof buf, "%8.2f Hz  Q %.3f", d.hi[i].f, d.hi[i].Q);
            row("HI", buf);
        }
        // Achieved error as a separate footer line below a small gap.
        y += 6.0;
        const char* errLabel = "max err ";
        c->setFontColor(labelColor);
        c->drawString(errLabel, CRect(x0, y, r.right - 2, y + rowH), kLeftText);
        const CCoord errLabelW = c->getStringWidth(errLabel);
        std::snprintf(buf, sizeof buf, "%.2f deg @ %.4g kHz%s",
                      d.maxErrorDeg, d.sampleRate / 1000.0,
                      d.converged ? "" : " (fallback)");
        c->setFontColor(valueColor);
        c->drawString(buf, CRect(x0 + errLabelW, y, r.right - 2, y + rowH), kLeftText);
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
