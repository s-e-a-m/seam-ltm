# libseamcalbus — the peer-aware calibration bus (Spec 2).
#
# This is the ONLY compiled artefact in _common/ (seam_meter.h, seam_haar.h et
# al. are header-only). It has to be a shared library and not a header: static
# state does not cross .vst3 module boundaries, and multipink, ltglide and strx
# are three separate bundles. See seam_calbus.h for the full rationale.
add_library(seam_calbus SHARED seam_calbus.cpp)

set_target_properties(seam_calbus PROPERTIES
    OUTPUT_NAME seamcalbus
)
target_compile_features(seam_calbus PRIVATE cxx_std_17)
target_include_directories(seam_calbus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The VST3 targets are built universal by the SDK (SMTG_BUILD_UNIVERSAL_BINARY,
# default ON -> XCODE_ATTRIBUTE_OSX_ARCHITECTURES "x86_64;arm64;arm64e").
# seam_calbus is a plain add_library, so it would otherwise inherit
# CMAKE_OSX_ARCHITECTURES, which the root CMakeLists pins to the host — leaving
# an x86_64-only dylib that an arm64 host cannot dlopen. The plugins would then
# load and play while strx read "calbus unavailable" and the bus silently did
# not exist.
#
# Call the SDK's own public function rather than hand-writing the property, so
# the dylib and the plugins cannot drift apart if the SDK's policy changes.
if(APPLE)
    smtg_target_setup_universal_binary(seam_calbus)
endif()

# Install into the user domain so no sudo is needed, consistent with VST3
# bundles landing in ~/Library/Audio/Plug-Ins/VST3. The client searches
# SEAM_CALBUS_PATH, then ~/Library/Application Support/SEAM, then /Library/...
if(APPLE)
    set(SEAM_CALBUS_INSTALL_DIR "$ENV{HOME}/Library/Application Support/SEAM")
    add_custom_command(TARGET seam_calbus POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SEAM_CALBUS_INSTALL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:seam_calbus> "${SEAM_CALBUS_INSTALL_DIR}/"
        COMMENT "[SEAM] Installing libseamcalbus.dylib to ${SEAM_CALBUS_INSTALL_DIR}"
    )
endif()
