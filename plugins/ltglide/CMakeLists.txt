cmake_minimum_required(VERSION 3.25.0)

project(seam-ltglide
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM LTGLIDE – Linkwitz glissando shaped tone-burst generator"
)

set(ltglide_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/ltglide_ids.h
    source/ltglide_dsp.h
    source/ltglide_processor.cpp
    source/ltglide_processor.h
    source/version.h
    resource/ltglide.uidesc
)

set(target ltglide)

smtg_add_vst3plugin(${target} ${ltglide_sources})
smtg_target_configure_version_file(${target})

target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../_common/calbus
)
add_dependencies(${target} seam_calbus)

target_compile_features(${target} PUBLIC cxx_std_17)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/ltglide.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.ltglide"
        COMPANY_NAME      "SEAM")
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
