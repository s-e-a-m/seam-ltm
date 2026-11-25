# libseamhub — the SEAM message hub.
#
# The suite's in-process message hub (it replaced the suite's earlier bus). A shared library, not a
# header, because static state does not cross .vst3 module boundaries.
# See seam_hub.h for the rationale.
add_library(seam_hub SHARED seam_hub.cpp)

set_target_properties(seam_hub PROPERTIES
    OUTPUT_NAME seamhub
)
target_compile_features(seam_hub PRIVATE cxx_std_17)
target_include_directories(seam_hub PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The VST3 targets are built universal by the SDK (SMTG_BUILD_UNIVERSAL_BINARY,
# default ON -> XCODE_ATTRIBUTE_OSX_ARCHITECTURES "x86_64;arm64;arm64e").
# seam_hub is a plain add_library, so it would otherwise inherit
# CMAKE_OSX_ARCHITECTURES, which the root CMakeLists pins to the host — leaving
# an x86_64-only dylib that an arm64 host cannot dlopen. The plugins would then
# load and play while strx read "hub unavailable" and the bus silently did
# not exist.
#
# Call the SDK's own public function rather than hand-writing the property, so
# the dylib and the plugins cannot drift apart if the SDK's policy changes.
if(APPLE)
    smtg_target_setup_universal_binary(seam_hub)
endif()

# Install into the user domain so no sudo is needed, consistent with VST3
# bundles landing in ~/Library/Audio/Plug-Ins/VST3. The client searches
# SEAM_HUB_PATH, then ~/Library/Application Support/SEAM, then /Library/...
#
# ONLY FROM THE TREE THAT FEEDS THE DAW: plugins built, and the SDK's links
# into ~/Library/Audio/Plug-Ins/VST3 made (SMTG_CREATE_PLUGIN_LINK). The
# installed dylib is the one every loaded plugin talks through, so a tests-only
# tree or a throwaway plugin tree (build-test, build-hub) installing its own
# would swap the hub under the DAW's plugins, possibly for one of another
# layout. Tests never need it: they link this target, or point SEAM_HUB_PATH
# at $<TARGET_FILE:seam_hub> (tests/CMakeLists.txt). Both variables are
# defined before this directory is added (root CMakeLists.txt).
if(APPLE AND SEAM_BUILD_PLUGINS AND SMTG_CREATE_PLUGIN_LINK)
    set(SEAM_HUB_INSTALL_DIR "$ENV{HOME}/Library/Application Support/SEAM")
    add_custom_command(TARGET seam_hub POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SEAM_HUB_INSTALL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:seam_hub> "${SEAM_HUB_INSTALL_DIR}/"
        COMMENT "[SEAM] Installing libseamhub.dylib to ${SEAM_HUB_INSTALL_DIR}"
    )
else()
    message(STATUS "[SEAM] libseamhub.dylib is not installed from this tree "
                   "(it installs only with SEAM_BUILD_PLUGINS=ON and SMTG_CREATE_PLUGIN_LINK=ON)")
endif()
