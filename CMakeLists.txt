cmake_minimum_required(VERSION 3.25.0)

# ─────────────────────────────────────────────────────────────────────────────
# SEAM-LTM — Learning Through Making
# A pedagogical VST3 plugin suite for the SEAM project
# ─────────────────────────────────────────────────────────────────────────────

project(seam-ltm
    VERSION 0.1.1
    DESCRIPTION "SEAM – Learning Through Making – VST3 Plugin Suite"
)

# ─── VST3 SDK ────────────────────────────────────────────────────────────────
# Path to the VST3 SDK (cloned in seam/sdk/vst3sdk)
set(vst3sdk_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/vst3sdk")

if(NOT EXISTS "${vst3sdk_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "VST3 SDK not found at ${vst3sdk_SOURCE_DIR}\n"
        "Clone it with: git clone --recursive https://github.com/steinbergmedia/vst3sdk.git"
    )
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
