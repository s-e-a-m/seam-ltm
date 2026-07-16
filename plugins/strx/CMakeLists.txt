cmake_minimum_required(VERSION 3.25.0)

project(seam-strx
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM strx – STONE observation M/S analyzer"
)

set(strx_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/strx_ids.h
    source/strx_dsp.h
    source/strx_goniometer.h
    source/strx_meters.h
    source/strx_spectrum.h
    source/strx_processor.cpp
    source/strx_processor.h
    source/version.h
    resource/strx.uidesc
)

set(target strx)

smtg_add_vst3plugin(${target} ${strx_sources})
smtg_target_configure_version_file(${target})

target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../_common)
target_compile_features(${target} PUBLIC cxx_std_17)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/strx.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.strx"
        COMPANY_NAME      "SEAM"
    )
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
