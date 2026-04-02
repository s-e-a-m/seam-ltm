cmake_minimum_required(VERSION 3.25.0)

project(seam-m2xhgr
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM M2XHGR – Mono to AmbiX via Haar Wavelet"
)

set(m2xhgr_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/m2xhgr_ids.h
    source/m2xhgr_processor.cpp
    source/m2xhgr_processor.h
    source/version.h
    resource/m2xhgr.uidesc
)

set(target m2xhgr)

smtg_add_vst3plugin(${target} ${m2xhgr_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/m2xhgr.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/resource/seam_logo.png
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
endif()

if(SMTG_MAC)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
endif()

if(SMTG_MAC)
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.m2xhgr"
        COMPANY_NAME      "SEAM"
    )
endif()
