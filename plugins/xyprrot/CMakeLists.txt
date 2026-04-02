cmake_minimum_required(VERSION 3.25.0)

project(seam-xyprrot
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM XYPRrot – First-order AmbiX Rotation"
)

set(xyprrot_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/xyprrot_ids.h
    source/xyprrot_processor.cpp
    source/xyprrot_processor.h
    source/version.h
    resource/xyprrot.uidesc
)

set(target xyprrot)

smtg_add_vst3plugin(${target} ${xyprrot_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

# Include path for shared common headers (seam_rotation.h, etc.)
target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)

# Plugin resources (UI description + images)
smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/xyprrot.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/resource/seam_logo.png
)

# Platform-specific entry point
if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
endif()

# Export the required symbols (bundleEntry, bundleExit, GetPluginFactory)
if(SMTG_MAC)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
endif()

if(SMTG_MAC)
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.xyprrot"
        COMPANY_NAME      "SEAM"
    )
endif()
