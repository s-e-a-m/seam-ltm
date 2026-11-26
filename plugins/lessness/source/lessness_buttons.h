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
#include "vstgui/lib/cvstguitimer.h"

#include "seam_hub_client.h"
#include "seam_hub_sheet.h"

#include <memory>
#include <string>
#include <utility>


namespace Seam {

// A framed, ALL CAPS label that does something when clicked. The press
// lights it in SliderActive, the suite's blue, and the act runs on release
// inside the frame; the light then stays kLitMs, so even a quick click is
// seen, since NEW SHEET otherwise changes only the footer's path.
class LessnessButton : public VSTGUI::CView {
public:
    static constexpr uint32_t kLitMs = 200;

    LessnessButton(const VSTGUI::CRect& size, VSTGUI::CFontRef font, std::string label,
                   const VSTGUI::CColor& structureColor, const VSTGUI::CColor& textColor,
                   const VSTGUI::CColor& activeColor)
        : VSTGUI::CView(size), font_(font), label_(std::move(label)),
          structureColor_(structureColor), textColor_(textColor), activeColor_(activeColor) {
        if (font_) font_->remember();
        setMouseEnabled(true);
    }

    ~LessnessButton() override {
        if (unlight_) { unlight_->stop(); unlight_->forget(); }
        if (font_) font_->forget();
    }

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                          const VSTGUI::CButtonState& buttons) override {
        if (!buttons.isLeftButton()) return VSTGUI::kMouseEventNotHandled;
        if (!getViewSize().pointInside(where)) return VSTGUI::kMouseEventNotHandled;
        setLit(true);
        return VSTGUI::kMouseEventHandled;      // so the release comes here
    }

    VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where,
                                        const VSTGUI::CButtonState& /*buttons*/) override {
        if (!lit_) return VSTGUI::kMouseEventNotHandled;
        if (getViewSize().pointInside(where)) {
            clicked();
            unlightLater();
        } else {
            setLit(false);                      // released outside: nothing done
        }
        return VSTGUI::kMouseEventHandled;
    }

    VSTGUI::CMouseEventResult onMouseCancel() override {
        setLit(false);
        return VSTGUI::kMouseEventHandled;
    }

    void draw(VSTGUI::CDrawContext* c) override {
        using namespace VSTGUI;
        CView::draw(c);
        c->setDrawMode(kAntiAliasing);
        const CRect r = getViewSize();
        if (lit_) {
            c->setFillColor(activeColor_);
            c->drawRect(r, kDrawFilled);
        }
        c->setFrameColor(lit_ ? activeColor_ : structureColor_);
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
    void setLit(bool lit) {
        if (lit_ == lit) return;
        lit_ = lit;
        invalid();
    }

    // One-shot: the timer stops itself on its first tick.
    void unlightLater() {
        if (!unlight_)
            unlight_ = new VSTGUI::CVSTGUITimer(
                [this](VSTGUI::CVSTGUITimer* t) { t->stop(); setLit(false); }, kLitMs,
                /*doStart*/false);
        unlight_->stop();
        unlight_->start();
    }

    VSTGUI::CFontRef      font_ = nullptr;
    std::string           label_;
    VSTGUI::CColor        structureColor_, textColor_, activeColor_;
    bool                  lit_ = false;
    VSTGUI::CVSTGUITimer* unlight_ = nullptr;
};

// FOLDER: chooses the folder of the next sheet. The current sheet keeps
// being written where it is; the footer says where the next one will go,
// and NEW SHEET starts it there.
class LessnessFolderButton : public LessnessButton {
public:
    LessnessFolderButton(const VSTGUI::CRect& size, VSTGUI::CFontRef font,
                         const VSTGUI::CColor& structureColor, const VSTGUI::CColor& textColor,
                         const VSTGUI::CColor& activeColor,
                         std::shared_ptr<std::string> nextFolder)
        : LessnessButton(size, font, "FOLDER", structureColor, textColor, activeColor),
          nextFolder_(std::move(nextFolder)) {}

protected:
    // The folder type is not a filter but a workaround. With no extension,
    // VSTGUI's Cocoa selector sets allowedContentTypes to nil, which AppKit
    // rejects ("contentTypes != nil", NSSavePanel.m): the exception is
    // swallowed by the host's event loop and no panel ever appears (seen in
    // Reaper, 2026-11-26). public.folder makes the list non-empty and leaves
    // files greyed out, which suits a folder chooser anyway.
    //
    // Modal, in a window of its own: a sheet on the plugin's window depends
    // on what the host's window is, and the asynchronous run's callback can
    // outlive the editor.
    void clicked() override {
        using namespace VSTGUI;
        CNewFileSelector* selector =
            CNewFileSelector::create(getFrame(), CNewFileSelector::kSelectDirectory);
        if (!selector) return;
        selector->setTitle("Folder of the next sheet");
        selector->addFileExtension(
            CFileExtension("Folders", "", "", 0, "public.folder"));
        const std::string start = currentFolder();
        if (!start.empty()) selector->setInitialDirectory(start.c_str());
        auto next = nextFolder_;
        if (selector->runModal() && selector->getNumSelectedFiles() > 0) {
            if (const UTF8StringPtr chosen = selector->getSelectedFile(0)) {
                std::string folder(chosen);
                while (folder.size() > 1 && folder.back() == '/') folder.pop_back();
                if (HubClient::instance().setSheetFolder(folder) && next) *next = folder;
            }
        }
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
};

// NEW SHEET: closes the session sheet and starts a new one (in the folder
// FOLDER chose, if one is waiting). The footer shows the new path.
class LessnessNewSheetButton : public LessnessButton {
public:
    LessnessNewSheetButton(const VSTGUI::CRect& size, VSTGUI::CFontRef font,
                           const VSTGUI::CColor& structureColor, const VSTGUI::CColor& textColor,
                           const VSTGUI::CColor& activeColor)
        : LessnessButton(size, font, "NEW SHEET", structureColor, textColor, activeColor) {}

protected:
    void clicked() override { HubClient::instance().newSheet(); }
};

} // namespace Seam
