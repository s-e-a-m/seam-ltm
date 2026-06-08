#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "x2uhj_dsp.h"

#include <cstdio>
#include <string>

namespace Seam {

// Read-only view: prints the live-designed (fc, Q) pairs and the achieved
// quadrature error for the current sample rate.
class QuadratureReadoutView : public VSTGUI::CView {
public:
    explicit QuadratureReadoutView(const VSTGUI::CRect& size, const x2uhj::UHJEncoder* enc)
        : VSTGUI::CView(size), encoder(enc) {}

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        CView::draw(c);
        if (!encoder) return;
        const auto& d = encoder->design();
        c->setFontColor(kWhiteCColor);
        CRect r = getViewSize();
        CCoord y = r.top + 2;
        char line[128];
        std::snprintf(line, sizeof line, "max err %.2f deg%s",
                      d.maxErrorDeg, d.converged ? "" : " (fallback)");
        c->drawString(line, CPoint(r.left + 4, y)); y += 14;
        for (int i = 0; i < d.nSections; ++i) {
            std::snprintf(line, sizeof line, "HR %8.2f Hz  Q %.3f", d.hr[i].f, d.hr[i].Q);
            c->drawString(line, CPoint(r.left + 4, y)); y += 12;
        }
        for (int i = 0; i < d.nSections; ++i) {
            std::snprintf(line, sizeof line, "HI %8.2f Hz  Q %.3f", d.hi[i].f, d.hi[i].Q);
            c->drawString(line, CPoint(r.left + 4, y)); y += 12;
        }
        setDirty(false);
    }
private:
    const x2uhj::UHJEncoder* encoder;
};

} // namespace Seam
