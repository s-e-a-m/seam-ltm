cmake_minimum_required(VERSION 3.25.0)

project(seam-sdmx
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM SDMX – Sum and Difference Matrix"
)

set(sdmx_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/sdmx_ids.h
    source/sdmx_processor.cpp
    source/sdmx_processor.h
    source/version.h
    resource/sdmx.uidesc
)

set(target sdmx)

smtg_add_vst3plugin(${target} ${sdmx_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/sdmx.uidesc
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
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.sdmx"
        COMPANY_NAME      "SEAM"
    )
endif()
