cmake_minimum_required(VERSION 3.25.0)

# ─────────────────────────────────────────────────────────────────────────────
# SEAM-LTM — Learning Through Making
# A pedagogical VST3 plugin suite for the SEAM project
# ─────────────────────────────────────────────────────────────────────────────

# ─── macOS architecture default ─────────────────────────────────────────────
# Xcode builds a universal binary (x86_64 + arm64) by default. On Macs where the
# system expat (MacPorts /opt/local, Homebrew /usr/local) is single-arch, the
# VSTGUI EXPAT dependency fails to link for the missing slice. Unless the user
# explicitly asks for a universal/cross build, pin to the host architecture.
# Must be set BEFORE project() so the toolchain is probed for the right arch.
# See: https://github.com/s-e-a-m/seam-ltm/issues/3
if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}"
        CACHE STRING "macOS build architecture (defaults to host)")
endif()

project(seam-ltm
    VERSION 0.1.5
    DESCRIPTION "SEAM – Learning Through Making – VST3 Plugin Suite"
)

# ─── Unit tests (SDK-free DSP cores) ─────────────────────────────────────────
option(SEAM_BUILD_TESTS "Build SEAM-LTM unit tests" ON)
if(SEAM_BUILD_TESTS)
    enable_testing()
endif()

# ─── VST3 SDK ────────────────────────────────────────────────────────────────
# Default: ../vst3sdk (SDK cloned alongside seam-ltm).
# Override: cmake -B build -DSEAM_VST3SDK_DIR=/your/path/to/vst3sdk
set(SEAM_VST3SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vst3sdk"
    CACHE PATH "Path to the VST3 SDK")
set(vst3sdk_SOURCE_DIR "${SEAM_VST3SDK_DIR}")

if(NOT EXISTS "${vst3sdk_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "VST3 SDK not found at ${vst3sdk_SOURCE_DIR}\n"
        "Clone it with:\n"
        "  git clone --recursive https://github.com/steinbergmedia/vst3sdk.git ../vst3sdk\n"
        "Or specify a custom path:\n"
        "  cmake -B build -DSEAM_VST3SDK_DIR=/path/to/vst3sdk"
    )
endif()

# ─── GCC 15 fix ─────────────────────────────────────────────────────────────
# VSTGUI's iplatformtaskexecutor.h uses uint64_t without including <cstdint>.
# GCC 15 no longer provides it transitively. We patch the file at configure
# time so that the build works out of the box on Arch/Manjaro/Fedora.
# Upstream issue: https://github.com/steinbergmedia/vstgui/issues/XXX
set(_taskexec_header "${vst3sdk_SOURCE_DIR}/vstgui4/vstgui/lib/platform/iplatformtaskexecutor.h")
if(EXISTS "${_taskexec_header}")
    file(READ "${_taskexec_header}" _taskexec_content)
    if(NOT _taskexec_content MATCHES "#include <cstdint>")
        string(REPLACE
            "#include <functional>"
            "#include <cstdint>\n#include <functional>"
            _taskexec_patched "${_taskexec_content}")
        file(WRITE "${_taskexec_header}" "${_taskexec_patched}")
        message(STATUS "[SEAM] Patched iplatformtaskexecutor.h — added #include <cstdint> (GCC 15 fix)")
    endif()
endif()

# VSTGUI for custom plugin GUI (logo + knobs)
option(SMTG_ENABLE_VSTGUI_SUPPORT "Enable VSTGUI Support" ON)
option(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES "Enable VST 3 Plug-in Examples" OFF)
option(SMTG_ENABLE_VST3_HOSTING_EXAMPLES "Enable VST 3 Hosting Examples" OFF)

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)

# Disable the VSTGUI live editor ("Open UI Editor" button) — not for end users.
add_compile_definitions(VSTGUI_LIVE_EDITING=0)

# ─── Plugins ─────────────────────────────────────────────────────────────────
add_subdirectory(plugins/sdmx)
add_subdirectory(plugins/xyprrot)
add_subdirectory(plugins/b2xrot)
add_subdirectory(plugins/lr2xhgr)
add_subdirectory(plugins/m2xhgr)
add_subdirectory(plugins/bamodulex)
add_subdirectory(plugins/ddelay)
add_subdirectory(plugins/multipink)
add_subdirectory(plugins/x2uhj)
add_subdirectory(plugins/abmodulex)

if(SEAM_BUILD_TESTS)
    add_subdirectory(tests)
endif()
