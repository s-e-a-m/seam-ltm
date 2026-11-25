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

# ─── macOS deployment target ────────────────────────────────────────────────
# Left unset, CMake initialises this from the macOS of the machine running the
# build, and every binary refuses to load on anything older — silently, with a
# prohibitory badge in the Finder and no diagnostic anywhere. v0.2.0 and v0.3.0
# both shipped pinned to 15.7 that way, unusable on a colleague's 15.6.1.
# 11.0 is the floor the suite ships for: the arm64 slice of a universal binary
# cannot go below it, and the VST3 SDK supports macOS 10.13-15.
# `tools/check-minos.py` holds the same number independently and fails the test
# suite if a build ever declares a newer one.
# Must be set BEFORE project(), like the architecture above.
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0"
        CACHE STRING "Minimum macOS the binaries run on")
endif()

project(seam-ltm
    VERSION 0.7.0
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

# The VSTGUI live editor ("Open UIDescription Editor") is gated by the macro
# VSTGUI_LIVE_EDITING, which is consumed only inside the vstgui library's own
# translation units (added just above). vstgui enables it per-config — on in
# Debug, off in Release (vstgui_init.cmake) — so finished GUIs ship without the
# editor while Debug builds keep it for design work. Defining the macro on our
# plugin targets here had no effect (they never test it), so it is omitted.

# ─── Plugins ─────────────────────────────────────────────────────────────────
# smtg_add_vst3plugin installs a POST_BUILD symlink into
# ~/Library/Audio/Plug-Ins/VST3, and the LAST build tree to compile a target
# owns that symlink. So a throwaway build tree that also builds plugins will
# silently hijack the links the DAW loads — and when that tree is deleted, the
# plugin vanishes from the host. This has bitten twice (dslar → /tmp, 2026-07-14;
# multipink → build-test, 2026-07-17).
#
# Turn this OFF for any build tree that is not the one feeding the DAW:
#   cmake -B build-test -DSEAM_BUILD_TESTS=ON -DSEAM_BUILD_PLUGINS=OFF
# The unit tests need only the SDK-free cores (headers) and libseamhub, both
# of which are configured outside the plugin block below.
# Declared before the hub's directory: the hub installs its dylib only from a
# tree that builds plugins and links them (plugins/_common/hub/CMakeLists.txt).
option(SEAM_BUILD_PLUGINS "Build the VST3 plugins (owns the ~/Library/Audio/Plug-Ins/VST3 symlinks)" ON)

# ─── Shared compiled libraries ───────────────────────────────────────────────
add_subdirectory(plugins/_common/hub)

# ─── The VST validator, kept out of the operator's sheets ───────────────────
# The SDK runs its validator as a POST_BUILD step of every plugin
# (SMTG_RUN_VST_VALIDATOR, SMTG_AddVST3Library.cmake). The validator loads and
# activates the plugin; a hub member then registers, which starts a session,
# and the hub writes a sheet into the operator's folder (~/Documents/SEAM/sheets
# by default) on every build. Build noise must not land among the sheets of
# real sessions. So the SDK's step is turned off here (a normal variable, which
# shadows the cache entry for every plugin directory below), and the same
# validator runs after each plugin with SEAM_HUB_CONF naming a conf whose
# folder is in this tree, and SEAM_HUB_PATH the dylib this tree just built
# (the installed one may have another layout, or not exist: only the DAW's
# tree installs it, plugins/_common/hub/CMakeLists.txt). The cache entry stays
# the switch: -DSMTG_RUN_VST_VALIDATOR=OFF turns ours off too.
set(SEAM_RUN_VST_VALIDATOR $CACHE{SMTG_RUN_VST_VALIDATOR})
set(SMTG_RUN_VST_VALIDATOR OFF)

set(SEAM_PLUGINS
    sdmx xyprrot b2xrot lr2xhgr m2xhgr bamodulex bastooge ddelay addelay
    multipink x2uhj abmodulex hilbert ltburst ltglide dslar strx lmo
    stunedrev alogos delrm choir)

if(SEAM_BUILD_PLUGINS)
    foreach(_p IN LISTS SEAM_PLUGINS)
        add_subdirectory(plugins/${_p})
    endforeach()

    if(SEAM_RUN_VST_VALIDATOR AND TARGET validator)
        set(_hubconf "${CMAKE_BINARY_DIR}/validator-hub.conf")
        file(WRITE "${_hubconf}" "sheet-folder=${CMAKE_BINARY_DIR}/validator-sheets\n")
        foreach(_p IN LISTS SEAM_PLUGINS)
            get_property(_targets DIRECTORY plugins/${_p} PROPERTY BUILDSYSTEM_TARGETS)
            foreach(_t IN LISTS _targets)
                get_target_property(_pkg ${_t} SMTG_PLUGIN_PACKAGE_PATH)
                if(NOT _pkg)
                    continue()
                endif()
                # POST_BUILD cannot be attached from this directory (a TARGET
                # build event must live in the target's own directory), so the
                # step is a custom command on a stamp that depends on the
                # plugin: it reruns whenever the plugin is rebuilt.
                set(_stamp "${CMAKE_BINARY_DIR}/validator/${_t}-$<CONFIG>.stamp")
                add_custom_command(
                    OUTPUT  "${_stamp}"
                    COMMAND ${CMAKE_COMMAND} -E echo "[SEAM] Validator started (${_t})..."
                    COMMAND ${CMAKE_COMMAND} -E env
                            "SEAM_HUB_CONF=${_hubconf}"
                            "SEAM_HUB_PATH=$<TARGET_FILE:seam_hub>"
                            $<TARGET_FILE:validator> "${_pkg}"
                    COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
                    DEPENDS ${_t} seam_hub validator
                    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
                    VERBATIM)
                add_custom_target(${_t}_validate ALL DEPENDS "${_stamp}")
            endforeach()
        endforeach()
    endif()
else()
    message(STATUS "[SEAM] SEAM_BUILD_PLUGINS=OFF — plugins skipped; VST3 symlinks left untouched")
endif()

if(SEAM_BUILD_TESTS)
    add_subdirectory(tests)
endif()
