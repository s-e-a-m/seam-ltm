//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · lessness — FOLDER and NEW SHEET, the two things it changes.
//
// GUI actions on views with no bound parameter, as strx's SAVE TABLE
// (strx_savebutton.h, and doc/style/ui-style.md on SAVE): neither is a
// plugin state the host should know about, neither needs the processor, and
// a momentary parameter would not survive a host that coalesces the press
// and the release. The click does the work here, on the GUI thread, through
// HubClient, which is a no-op in null mode.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfileselector.h"

#include "seam_hub_client.h"
#include "seam_hub_sheet.h"

#include <memory>
#include <string>
#include <utility>

namespace Seam {

// A framed, ALL CAPS label that does something when clicked.
class LessnessButton : public VSTGUI::CView {
public:
    LessnessButton(const VSTGUI::CRect& size, VSTGUI::CFontRef font, std::string label,
                   const VSTGUI::CColor& structureColor, const VSTGUI::CColor& textColor)
        : VSTGUI::CView(size), font_(font), label_(std::move(label)),
          structureColor_(structureColor), textColor_(textColor) {
        if (font_) font_->remember();
        setMouseEnabled(true);
    }

    ~LessnessButton() override { if (font_) font_->forget(); }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                          const VSTGUI::CButtonState& buttons) override {
        if (!buttons.isLeftButton()) return VSTGUI::kMouseEventNotHandled;
        if (!getViewSize().pointInside(where)) return VSTGUI::kMouseEventNotHandled;
        clicked();
        return VSTGUI::kMouseEventHandled;
    }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        CView::draw(c);
        c->setDrawMode(kAntiAliasing);
        const CRect r = getViewSize();
        c->setFrameColor(structureColor_);
        c->setLineWidth(1.0);
        c->drawRect(r, kDrawStroked);
        if (font_) c->setFont(font_);
        c->setFontColor(textColor_);
        c->drawString(label_.c_str(), r, kCenterText);
        setDirty(false);
    }

protected:
    virtual void clicked() = 0;

private:
    VSTGUI::CFontRef font_ = nullptr;
    std::string      label_;
    VSTGUI::CColor   structureColor_, textColor_;
};

// FOLDER: chooses the folder of the next sheet. The current sheet keeps
// being written where it is; the footer says where the next one will go,
// and NEW SHEET starts it there.
class LessnessFolderButton : public LessnessButton {
public:
    LessnessFolderButton(const VSTGUI::CRect& size, VSTGUI::CFontRef font,
                         const VSTGUI::CColor& structureColor, const VSTGUI::CColor& textColor,
                         std::shared_ptr<std::string> nextFolder)
        : LessnessButton(size, font, "FOLDER", structureColor, textColor),
          nextFolder_(std::move(nextFolder)) {}

protected:
    void clicked() override {
        using namespace VSTGUI;
        if (*open_) return;                   // one chooser at a time
        CNewFileSelector* selector =
            CNewFileSelector::create(getFrame(), CNewFileSelector::kSelectDirectory);
        if (!selector) return;
        selector->setTitle("Folder of the next sheet");
        const std::string start = currentFolder();
        if (!start.empty()) selector->setInitialDirectory(start.c_str());
        // Asynchronous: the callback may run after this view, or the whole
        // editor, is gone, so it captures what it needs by value and never
        // `this`. The selector keeps itself alive until it has called back.
        *open_ = true;
        auto next = nextFolder_;
        auto open = open_;
        const bool started = selector->run([next, open](CNewFileSelector* s) {
            *open = false;
            if (!s || s->getNumSelectedFiles() == 0) return;   // cancelled
            const UTF8StringPtr chosen = s->getSelectedFile(0);
            if (!chosen) return;
            std::string folder(chosen);
            while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
            if (HubClient::instance().setSheetFolder(folder) && next) *next = folder;
        });
        if (!started) *open_ = false;
        selector->forget();
    }

private:
    // Where the chooser opens: the folder of the sheet being written, if any.
    static std::string currentFolder() {
        const SeamHubSheetStatus s = HubClient::instance().sheetStatus();
        const std::string path = hubsheet::detail::oneLine(s.path, sizeof s.path);
        const size_t slash = path.find_last_of('/');
        return (slash == std::string::npos || slash == 0) ? std::string() : path.substr(0, slash);
    }

    std::shared_ptr<std::string> nextFolder_;
    std::shared_ptr<bool>        open_ = std::make_shared<bool>(false);
};

// NEW SHEET: closes the session sheet and starts a new one (in the folder
// FOLDER chose, if one is waiting). The footer shows the new path.
class LessnessNewSheetButton : public LessnessButton {
public:
    LessnessNewSheetButton(const VSTGUI::CRect& size, VSTGUI::CFontRef font,
                           const VSTGUI::CColor& structureColor, const VSTGUI::CColor& textColor)
        : LessnessButton(size, font, "NEW SHEET", structureColor, textColor) {}

protected:
    void clicked() override { HubClient::instance().newSheet(); }
};

} // namespace Seam
