cmake_minimum_required(VERSION 3.25.0)

project(seam-dslar
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM DSLAR – Di Scipio LAR feedforward homeostatic processor"
)

set(dslar_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/dslar_ids.h
    source/dslar_dsp.h
    source/dslar_reset_button.h
    source/dslar_processor.cpp
    source/dslar_processor.h
    source/version.h
    resource/dslar.uidesc
)

set(target dslar)

smtg_add_vst3plugin(${target} ${dslar_sources})
smtg_target_configure_version_file(${target})

target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../_common)
target_compile_features(${target} PUBLIC cxx_std_17)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/dslar.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.dslar"
        COMPANY_NAME      "SEAM"
    )
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
