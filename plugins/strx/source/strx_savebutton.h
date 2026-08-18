#pragma once
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "strx_processor.h"
#include "strx_export.h"

#include <cstdio>
#include <ctime>
#include <string>

namespace Seam {

// SAVE TABLE — writes the current band table to a file, on the GUI thread.
//
// Not a parameter, and deliberately. A save is a momentary action, and a
// momentary parameter is unreliable through a host: some coalesce the whole
// press and release into one point and the rising edge never arrives (see
// ltglide, where a momentary trigger had to be abandoned for this reason).
// Nothing about writing a file needs the processor, the host, or automation:
// the frame is already cached on this thread, so the click does the work here
// and the audio thread never learns that it happened.
//
// The last outcome stays on screen until the next save, rather than fading:
// it is the receipt, and the operator wants to be able to look up at it.
class StrxSaveButton : public VSTGUI::CView {
public:
    static constexpr double kBoxWidth = 150.0;

    StrxSaveButton(const VSTGUI::CRect& size, StrxProcessor* processor,
                   VSTGUI::CFontRef font, const std::string& directory,
                   const VSTGUI::CColor& structureColor, const VSTGUI::CColor& textColor)
        : VSTGUI::CView(size), processor_(processor), font_(font), directory_(directory),
          structureColor_(structureColor), textColor_(textColor) {
        if (font_) font_->remember();
        setMouseEnabled(true);
        status_ = "writes to " + directory_;
    }

    ~StrxSaveButton() override { if (font_) font_->forget(); }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                          const VSTGUI::CButtonState& buttons) override {
        using namespace VSTGUI;
        if (!buttons.isLeftButton()) return kMouseEventNotHandled;
        const CRect box = boxRect();
        if (!box.pointInside(where)) return kMouseEventNotHandled;
        save();
        invalid();
        return kMouseEventHandled;
    }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        CView::draw(c);
        c->setDrawMode(kAntiAliasing);
        if (font_) c->setFont(font_);

        const CRect box = boxRect();
        c->setFrameColor(structureColor_);
        c->setLineWidth(1.0);
        c->drawRect(box, kDrawStroked);
        c->setFontColor(textColor_);
        c->drawString("SAVE TABLE", CRect(box.left, box.top + 5, box.right, box.bottom - 3),
                       kCenterText);

        const CRect r = getViewSize();
        CColor dim = textColor_;
        dim.alpha = 160;
        c->setFontColor(ok_ ? textColor_ : dim);
        c->drawString(status_.c_str(),
                       CRect(box.right + 10, r.top + 5, r.right, r.bottom - 3), kLeftText);
    }

private:
    VSTGUI::CRect boxRect() const {
        const VSTGUI::CRect r = getViewSize();
        return VSTGUI::CRect(r.left, r.top, r.left + kBoxWidth, r.bottom);
    }

    // A compact description of what is sounding, for the file's `source` field.
    // StrxStatusLine has its own richer formatter for the screen, which carries
    // collision flags and sweep parameters this does not need.
    std::string describeSource(strx::ExportContext& ctx) const {
        if (!processor_) return "no processor";
        const auto& d = processor_->calbusWatch().poll();
        if (!d.available)      return "calibration bus not available";
        if (d.firstActive < 0) return "nothing sounding on the calibration bus";
        const SeamCalbusRecord& rec = processor_->calbusWatch().records()[d.firstActive];
        ctx.stoneId = (int)rec.stoneId;
        char buf[160];
        if (rec.kind == (uint32_t)kSeamCalbusPink)
            std::snprintf(buf, sizeof buf, "multipink - STONE %u - slot %d-%d - %.1f dB",
                          rec.stoneId, rec.u.pink.slotStart,
                          rec.u.pink.slotStart + rec.u.pink.slotCount - 1, rec.levelDb);
        else
            std::snprintf(buf, sizeof buf,
                          "ltglide - STONE %u - %.1f dB  (A SWEEP IS NOT A PINK FIELD: "
                          "this table is not a measurement)", rec.stoneId, rec.levelDb);
        return buf;
    }

    void save() {
        if (!processor_) { ok_ = false; status_ = "no processor"; return; }
        strx::ExportContext ctx;
        ctx.sampleRate = processor_->sampleRate();
        ctx.source     = describeSource(ctx);
        const std::time_t now = std::time(nullptr);
#if defined(_WIN32)
        localtime_s(&ctx.when, &now);
#else
        localtime_r(&now, &ctx.when);
#endif
        std::string path, error;
        if (strx::writeBandTable(directory_, processor_->latestFrame(), ctx, &path, &error)) {
            ok_ = true;
            const size_t slash = path.find_last_of('/');
            status_ = "saved  " + (slash == std::string::npos ? path : path.substr(slash + 1));
        } else {
            ok_ = false;
            status_ = error;
        }
    }

    StrxProcessor* processor_ = nullptr;
    VSTGUI::CFontRef font_ = nullptr;
    std::string directory_;
    VSTGUI::CColor structureColor_, textColor_;
    std::string status_;
    bool ok_ = false;
};

} // namespace Seam
