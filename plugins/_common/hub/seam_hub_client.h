//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · hub client — loads libseamhub and adapts its C ABI.
//
// Header-only, included by every plugin. One instance per .vst3 module
// (function-local static), which owns the dlopen handle for the module's
// lifetime.
//
// WHY dlopen AND NOT FDynLibrary: the SDK's loader takes a `tchar*` path, and
// UNICODE is defined by default (ftypes.h:30), so tchar is char16_t
// (ftypes.h:91). Converting a $HOME-derived filesystem path to UTF-16 only for
// the SDK to convert it back buys nothing. dlopen covers both platforms the
// suite builds for, and the handle lives in a static, so FDynLibrary's
// refcounting is unnecessary too.
//
// NEVER dlclose A LIBRARY IT HAS USED: once a library is loaded and its
// version matches, this client never dlcloses it. The hub may have detached a
// stuck writer thread at quit; unloading the library would unmap the code that
// thread is still running. Only a library rejected before any use (version
// mismatch or missing symbols) is dlclosed.
//
// DEGRADATION IS THE POINT: when the dylib is missing or its version does not
// match, the client enters null mode and every call becomes a silent no-op.
// The hub is an observer, and an observer that breaks the instrument is
// unacceptable in the room.
//
// HANDLES: callers MUST initialise their handle member to SEAM_HUB_NO_HANDLE,
// never to a default-constructed 0, which is a valid registry index.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "seam_hub.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if !defined(_WIN32)
  #include <dlfcn.h>
#endif

namespace Seam {

class HubClient {
public:
    // One per .vst3 module.
    static HubClient& instance() {
        static HubClient c;
        return c;
    }

    // For tests: a client that never loaded anything.
    static HubClient makeUnavailableForTest() { return HubClient(NullTag{}); }

    bool available() const { return hub_ != nullptr; }

    int32_t registerMember(const char* name, uint32_t says, uint32_t hears) {
        return available() ? register_(hub_, name, says, hears) : SEAM_HUB_NO_HANDLE;
    }

    void unregisterMember(int32_t handle) {
        if (available() && handle != SEAM_HUB_NO_HANDLE) unregister_(hub_, handle);
    }

    // RT-safe when the hub is available; a plain branch when it is not.
    void publishState(int32_t handle, const SeamHubState& s) {
        if (available() && handle != SEAM_HUB_NO_HANDLE) publish_(hub_, handle, &s);
    }

    int32_t members(SeamHubMember* out, int32_t maxCount) {
        return available() ? members_(hub_, out, maxCount) : 0;
    }

    uint64_t post(int32_t handle, SeamHubProposal& p) {
        if (!available() || handle == SEAM_HUB_NO_HANDLE) return 0;
        return post_(hub_, handle, &p);
    }

    int32_t proposals(uint64_t afterId, SeamHubProposal* out, int32_t maxCount) {
        return available() ? proposals_(hub_, afterId, out, maxCount) : 0;
    }

    int32_t lifecycle(int32_t receiver, uint64_t id) {
        if (!available() || receiver == SEAM_HUB_NO_HANDLE) return kSeamHubNone;
        return lifecycle_(hub_, receiver, id);
    }

    int32_t transition(int32_t receiver, uint64_t id, int32_t to, bool byOperator) {
        if (!available() || receiver == SEAM_HUB_NO_HANDLE) return kSeamHubNone;
        return transition_(hub_, receiver, id, to, byOperator ? 1 : 0);
    }

    SeamHubSheetStatus sheetStatus() {
        SeamHubSheetStatus s;
        std::memset(&s, 0, sizeof s);
        if (available()) sheet_status_(hub_, &s);
        return s;
    }

    bool setSheetFolder(const std::string& folder) {
        return available() && sheet_set_folder_(hub_, folder.c_str()) == 0;
    }

    void newSheet() {
        if (available()) sheet_new_(hub_);
    }

private:
    struct NullTag {};
    explicit HubClient(NullTag) {}

    HubClient() { load(); }

    void load() {
#if !defined(_WIN32)
        for (const std::string& path : candidatePaths()) {
            void* h = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!h) continue;

            // The version first: nothing else is looked up in a library that
            // might not speak this ABI.
            auto version = (uint32_t (*)(void))::dlsym(h, "seam_hub_v1_version");
            if (!version || version() != SEAM_HUB_VERSION) { ::dlclose(h); continue; }

            auto get = (SeamHub* (*)(void))::dlsym(h, "seam_hub_v1_get");
            register_   = (int32_t (*)(SeamHub*, const char*, uint32_t, uint32_t))
                          ::dlsym(h, "seam_hub_v1_register");
            unregister_ = (void (*)(SeamHub*, int32_t))::dlsym(h, "seam_hub_v1_unregister");
            publish_    = (void (*)(SeamHub*, int32_t, const SeamHubState*))
                          ::dlsym(h, "seam_hub_v1_publish_state");
            members_    = (int32_t (*)(SeamHub*, SeamHubMember*, int32_t))
                          ::dlsym(h, "seam_hub_v1_members");
            post_       = (uint64_t (*)(SeamHub*, int32_t, SeamHubProposal*))
                          ::dlsym(h, "seam_hub_v1_post");
            proposals_  = (int32_t (*)(SeamHub*, uint64_t, SeamHubProposal*, int32_t))
                          ::dlsym(h, "seam_hub_v1_proposals");
            lifecycle_  = (int32_t (*)(SeamHub*, int32_t, uint64_t))
                          ::dlsym(h, "seam_hub_v1_lifecycle");
            transition_ = (int32_t (*)(SeamHub*, int32_t, uint64_t, int32_t, int32_t))
                          ::dlsym(h, "seam_hub_v1_transition");
            sheet_status_ = (void (*)(SeamHub*, SeamHubSheetStatus*))
                          ::dlsym(h, "seam_hub_v1_sheet_status");
            sheet_set_folder_ = (int32_t (*)(SeamHub*, const char*))
                          ::dlsym(h, "seam_hub_v1_sheet_set_folder");
            sheet_new_  = (void (*)(SeamHub*))::dlsym(h, "seam_hub_v1_sheet_new");

            if (!get || !register_ || !unregister_ || !publish_ || !members_ || !post_ ||
                !proposals_ || !lifecycle_ || !transition_ || !sheet_status_ ||
                !sheet_set_folder_ || !sheet_new_) {
                clear();
                ::dlclose(h);   // rejected before use
                continue;
            }
            hub_ = get();
            if (!hub_) { clear(); ::dlclose(h); continue; }
            return;   // matched and in use: never dlclosed
        }
#endif
    }

    void clear() {
        register_ = nullptr; unregister_ = nullptr; publish_ = nullptr;
        members_ = nullptr; post_ = nullptr; proposals_ = nullptr;
        lifecycle_ = nullptr; transition_ = nullptr; sheet_status_ = nullptr;
        sheet_set_folder_ = nullptr; sheet_new_ = nullptr;
    }

    // When SEAM_HUB_PATH is set, it is the ONLY candidate tried — no fallback
    // to the installed locations. On a dev machine that has built seam_hub, a
    // real version-matching dylib is already installed under ~/Library, and a
    // fallback would silently swallow a deliberately mismatched dylib pointed
    // at by the override (the version-rejection test's stub).
    static std::vector<std::string> candidatePaths() {
        static const char* kLib =
#if defined(__APPLE__)
            "libseamhub.dylib";
#else
            "libseamhub.so";
#endif
        std::vector<std::string> out;
        if (const char* env = std::getenv("SEAM_HUB_PATH")) {
            // A directory or a full path to the library.
            std::string e(env);
            out.push_back(e);
            out.push_back(e + "/" + kLib);
            return out;
        }
        if (const char* home = std::getenv("HOME")) {
            out.push_back(std::string(home) + "/Library/Application Support/SEAM/" + kLib);
        }
        out.push_back(std::string("/Library/Application Support/SEAM/") + kLib);
        return out;
    }

    SeamHub* hub_ = nullptr;
    int32_t  (*register_)(SeamHub*, const char*, uint32_t, uint32_t) = nullptr;
    void     (*unregister_)(SeamHub*, int32_t) = nullptr;
    void     (*publish_)(SeamHub*, int32_t, const SeamHubState*) = nullptr;
    int32_t  (*members_)(SeamHub*, SeamHubMember*, int32_t) = nullptr;
    uint64_t (*post_)(SeamHub*, int32_t, SeamHubProposal*) = nullptr;
    int32_t  (*proposals_)(SeamHub*, uint64_t, SeamHubProposal*, int32_t) = nullptr;
    int32_t  (*lifecycle_)(SeamHub*, int32_t, uint64_t) = nullptr;
    int32_t  (*transition_)(SeamHub*, int32_t, uint64_t, int32_t, int32_t) = nullptr;
    void     (*sheet_status_)(SeamHub*, SeamHubSheetStatus*) = nullptr;
    int32_t  (*sheet_set_folder_)(SeamHub*, const char*) = nullptr;
    void     (*sheet_new_)(SeamHub*) = nullptr;
};

} // namespace Seam
