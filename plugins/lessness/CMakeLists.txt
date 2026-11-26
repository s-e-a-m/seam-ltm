cmake_minimum_required(VERSION 3.25.0)

project(seam-lessness
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM LESSNESS – Hub Detector"
)

set(lessness_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/lessness_ids.h
    source/lessness_view.h
    source/lessness_panes.h
    source/lessness_buttons.h
    source/lessness_processor.cpp
    source/lessness_processor.h
    source/version.h
    resource/lessness.uidesc
)

set(target lessness)

smtg_add_vst3plugin(${target} ${lessness_sources})
smtg_target_configure_version_file(${target})

target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../_common
    ${CMAKE_CURRENT_SOURCE_DIR}/../_common/hub
)
add_dependencies(${target} seam_hub)

target_compile_features(${target} PUBLIC cxx_std_17)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/lessness.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.lessness"
        COMPANY_NAME      "SEAM"
    )
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
