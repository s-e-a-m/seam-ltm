cmake_minimum_required(VERSION 3.25.0)

project(seam-stunedrev
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM STUNEDREV – SSCDO#2 tuned all-pass memories"
)

set(stunedrev_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/stunedrev_ids.h
    source/stunedrev_dsp.h
    source/stunedrev_params.h
    source/stunedrev_state.h
    source/stunedrev_views.h
    source/stunedrev_processor.cpp
    source/stunedrev_processor.h
    source/version.h
    resource/stunedrev.uidesc
)

set(target stunedrev)

smtg_add_vst3plugin(${target} ${stunedrev_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)
target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../_common
)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/stunedrev.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.stunedrev"
        COMPANY_NAME      "SEAM")
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
