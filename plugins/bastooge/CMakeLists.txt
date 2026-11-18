cmake_minimum_required(VERSION 3.25.0)

project(seam-bastooge
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM BASTOOGE – S.T.OOGE mk2g Decoder"
)

set(bastooge_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/bastooge_dsp.h
    source/bastooge_ids.h
    source/bastooge_processor.cpp
    source/bastooge_processor.h
    source/bastooge_readout_view.h
    source/bastooge_state.h
    source/version.h
    resource/bastooge.uidesc
)

set(target bastooge)

smtg_add_vst3plugin(${target} ${bastooge_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)
target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../_common
    ${CMAKE_CURRENT_SOURCE_DIR}/../bamodulex/source
)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/bastooge.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../_common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.bastooge"
        COMPANY_NAME      "SEAM"
    )
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
