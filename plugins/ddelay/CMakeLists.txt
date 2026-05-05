cmake_minimum_required(VERSION 3.25.0)

project(seam-ddelay
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM DDELAY – Quad Speaker-Alignment Delay"
)

set(ddelay_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/ddelay_ids.h
    source/ddelay_processor.cpp
    source/ddelay_processor.h
    source/version.h
    resource/ddelay.uidesc
)

set(target ddelay)

smtg_add_vst3plugin(${target} ${ddelay_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/ddelay.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.ddelay"
        COMPANY_NAME      "SEAM"
    )
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
