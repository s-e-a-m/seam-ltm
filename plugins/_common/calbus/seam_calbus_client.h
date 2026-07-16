//──────────────────────────────────────────────────────────────────────────
// SEAM-LTM · calbus client — loads libseamcalbus and adapts its C ABI.
//
// Header-only, included by multipink, ltglide and strx. One instance per
// .vst3 module (function-local static), which owns the dlopen handle for the
// module's lifetime.
//
// WHY dlopen AND NOT FDynLibrary: the SDK's loader takes a `tchar*` path, and
// UNICODE is defined by default (ftypes.h:30), so tchar is char16_t
// (ftypes.h:91). Converting a $HOME-derived filesystem path to UTF-16 only for
// the SDK to convert it back buys nothing. dlopen covers both platforms the
// suite builds for (macmain.cpp, linuxmain.cpp), and the handle lives in a
// static, so FDynLibrary's refcounting is unnecessary too.
//
// DEGRADATION IS THE POINT: when the dylib is missing or its version does not
// match, the client enters null mode and every call becomes a silent no-op.
// The bus is an observer, and an observer that breaks the instrument is
// unacceptable in the room. multipink still makes noise; strx still analyses.
//
// HANDLES: seam_calbus.h defines SEAM_CALBUS_NO_HANDLE (-1) as the one safe
// "unclaimed" sentinel. Callers (Tasks 3-5) MUST initialise their own handle
// member to SEAM_CALBUS_NO_HANDLE, never to a default-constructed 0 — a
// zero-initialised handle is index 0 / epoch 0, a token that looks exactly
// like a real registration and would publish into slot 0 from the audio
// thread. This wrapper follows the same rule internally: every "no handle"
// return and comparison below uses the named constant, never a bare -1.
//──────────────────────────────────────────────────────────────────────────
#pragma once

#include "seam_calbus.h"

#include <cstdlib>
#include <string>
#include <vector>

#if !defined(_WIN32)
  #include <dlfcn.h>
#endif

namespace Seam {

class CalbusClient {
public:
    // One per .vst3 module.
    static CalbusClient& instance() {
        static CalbusClient c;
        return c;
    }

    // For tests: a client that never loaded anything.
    static CalbusClient makeUnavailableForTest() { return CalbusClient(NullTag{}); }

    bool available() const { return bus_ != nullptr; }

    int32_t registerSlot() {
        return available() ? register_(bus_) : SEAM_CALBUS_NO_HANDLE;
    }

    void unregisterSlot(int32_t handle) {
        if (available() && handle != SEAM_CALBUS_NO_HANDLE) unregister_(bus_, handle);
    }

    // RT-safe when the bus is available; a plain branch when it is not.
    void publish(int32_t handle, const SeamCalbusRecord& rec) {
        if (available() && handle != SEAM_CALBUS_NO_HANDLE) publish_(bus_, handle, &rec);
    }

    int32_t snapshot(SeamCalbusRecord* out, int32_t maxCount) {
        return available() ? snapshot_(bus_, out, maxCount) : 0;
    }

private:
    struct NullTag {};
    explicit CalbusClient(NullTag) {}

    CalbusClient() { load(); }

    // The handle is intentionally never dlclose()d: the bus must outlive every
    // plugin instance in the module, and the OS drops it when the module goes.
    void load() {
#if !defined(_WIN32)
        for (const std::string& path : candidatePaths()) {
            void* h = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!h) continue;

            auto version = (uint32_t (*)(void))::dlsym(h, "seam_calbus_v1_version");
            if (!version || version() != SEAM_CALBUS_VERSION) { ::dlclose(h); continue; }

            auto get = (SeamCalbus* (*)(void))::dlsym(h, "seam_calbus_v1_get");
            register_   = (int32_t (*)(SeamCalbus*))::dlsym(h, "seam_calbus_v1_register");
            unregister_ = (void (*)(SeamCalbus*, int32_t))::dlsym(h, "seam_calbus_v1_unregister");
            publish_    = (void (*)(SeamCalbus*, int32_t, const SeamCalbusRecord*))
                          ::dlsym(h, "seam_calbus_v1_publish");
            snapshot_   = (int32_t (*)(SeamCalbus*, SeamCalbusRecord*, int32_t))
                          ::dlsym(h, "seam_calbus_v1_snapshot");

            if (!get || !register_ || !unregister_ || !publish_ || !snapshot_) {
                register_ = nullptr; unregister_ = nullptr;
                publish_ = nullptr; snapshot_ = nullptr;
                ::dlclose(h);
                continue;
            }
            bus_ = get();
            return;
        }
#endif
    }

    // NOTE (deviation from the Task 2 brief): when SEAM_CALBUS_PATH is set, it
    // is the ONLY candidate tried — no fallback to the installed locations.
    // The brief's original version always appended the HOME/global fallbacks
    // after the env candidates, which is wrong for an explicit override: on a
    // dev machine that has ever built seam_calbus, a real (version-matching)
    // dylib is already installed at ~/Library/Application Support/SEAM/ by
    // Task 1's CMake POST_BUILD step. A caller who points SEAM_CALBUS_PATH at
    // a *different*, deliberately mismatched dylib (e.g. the version-rejection
    // test's stub) would have the mismatch silently swallowed by that
    // fallback, defeating the whole point of the override. Discovered by
    // seam_calbus_version_test actually loading the real bus instead of
    // rejecting the stub. Production behaviour (no env var set) is unchanged.
    static std::vector<std::string> candidatePaths() {
        static const char* kLib =
#if defined(__APPLE__)
            "libseamcalbus.dylib";
#else
            "libseamcalbus.so";
#endif
        std::vector<std::string> out;
        if (const char* env = std::getenv("SEAM_CALBUS_PATH")) {
            // Accept either a directory or a full path to the library. This is
            // an explicit override, so it is the only thing tried.
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

    SeamCalbus* bus_ = nullptr;
    int32_t (*register_)(SeamCalbus*) = nullptr;
    void    (*unregister_)(SeamCalbus*, int32_t) = nullptr;
    void    (*publish_)(SeamCalbus*, int32_t, const SeamCalbusRecord*) = nullptr;
    int32_t (*snapshot_)(SeamCalbus*, SeamCalbusRecord*, int32_t) = nullptr;
};

} // namespace Seam
