cmake_minimum_required(VERSION 3.25.0)

project(seam-alogos
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM ALOGOS – irrational all-pass memory"
)

set(alogos_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/alogos_ids.h
    source/alogos_dsp.h
    source/alogos_arena.h
    source/alogos_decay.h
    source/alogos_text.h
    source/alogos_params.h
    source/alogos_state.h
    source/alogos_views.h
    source/alogos_processor.cpp
    source/alogos_processor.h
    source/version.h
    resource/alogos.uidesc
)

set(target alogos)

smtg_add_vst3plugin(${target} ${alogos_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)
target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../_common
)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/alogos.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.alogos"
        COMPANY_NAME      "SEAM")
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
