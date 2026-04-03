cmake_minimum_required(VERSION 3.25.0)

project(seam-b2xrot
    VERSION     ${CMAKE_PROJECT_VERSION}
    DESCRIPTION "SEAM B2Xrot – B-format to AmbiX with Rotation"
)

set(b2xrot_sources
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.h
    source/b2xrot_ids.h
    source/b2xrot_processor.cpp
    source/b2xrot_processor.h
    source/version.h
    resource/b2xrot.uidesc
)

set(target b2xrot)

smtg_add_vst3plugin(${target} ${b2xrot_sources})
smtg_target_configure_version_file(${target})

target_compile_features(${target} PUBLIC cxx_std_17)
target_link_libraries(${target} PRIVATE sdk vstgui_support)

target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common)

smtg_target_add_plugin_resources(${target}
    RESOURCES
        resource/b2xrot.uidesc
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/resource/seam_logo.png
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/resource/Fonts/SourceCodePro-Light.otf
)

if(SMTG_MAC)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
    smtg_target_set_exported_symbols(${target} "${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macexport.exp")
    smtg_target_set_bundle(${target}
        BUNDLE_IDENTIFIER "io.github.s-e-a-m.b2xrot"
        COMPANY_NAME      "SEAM"
    )
elseif(SMTG_LINUX)
    target_sources(${target} PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
