cmake_minimum_required(VERSION 3.25.0)

project(seam-abmodulex
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM ABMODULEX – A-format to AmbiX"
)

set(abmodulex_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/abmodulex_ids.h
    source/abmodulex_dsp.h
    source/abmodulex_processor.cpp
    source/abmodulex_processor.h
    source/version.h
    resource/abmodulex.uidesc
)

set(target abmodulex)

smtg_add_vst3plugin(${target} ${abmodulex_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/abmodulex.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.abmodulex"
        COMPANY_NAME      "SEAM")
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
