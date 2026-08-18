# SEAM-LTM unit tests (doctest; SDK-free DSP cores, except seam_state_test
# which links the SDK base/common layers to test the IBStream decode rule)

# seam_calbus_test spawns std::thread for its seqlock hammer/churn stress
# tests. Links fine without this on macOS/libc++ (pthread is pulled in
# transitively), but not on Linux, which this suite also targets
# (linuxmain.cpp exists in the plugin CMake files) — find and link it
# explicitly rather than relying on a platform-specific transitive link.
find_package(Threads REQUIRED)

add_executable(seam_tests
    smoke_test.cpp
)
target_include_directories(seam_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(seam_tests PRIVATE cxx_std_17)
add_test(NAME seam_tests COMMAND seam_tests)

add_executable(x2uhj_dsp_test
    x2uhj_dsp_test.cpp
)
target_include_directories(x2uhj_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/x2uhj/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(x2uhj_dsp_test PRIVATE cxx_std_17)
add_test(NAME x2uhj_dsp_test COMMAND x2uhj_dsp_test)

add_executable(abmodulex_dsp_test
    abmodulex_dsp_test.cpp
)
target_include_directories(abmodulex_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/abmodulex/source
)
target_compile_features(abmodulex_dsp_test PRIVATE cxx_std_17)
add_test(NAME abmodulex_dsp_test COMMAND abmodulex_dsp_test)

add_executable(seam_quadrature_test
    seam_quadrature_test.cpp
)
target_include_directories(seam_quadrature_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_quadrature_test PRIVATE cxx_std_17)
add_test(NAME seam_quadrature_test COMMAND seam_quadrature_test)

add_executable(hilbert_dsp_test
    hilbert_dsp_test.cpp
)
target_include_directories(hilbert_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/hilbert/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(hilbert_dsp_test PRIVATE cxx_std_17)
add_test(NAME hilbert_dsp_test COMMAND hilbert_dsp_test)

add_executable(seam_airabsorption_test
    seam_airabsorption_test.cpp
)
target_include_directories(seam_airabsorption_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_airabsorption_test PRIVATE cxx_std_17)
add_test(NAME seam_airabsorption_test COMMAND seam_airabsorption_test)

add_executable(lr2xhgr_dsp_test
    lr2xhgr_dsp_test.cpp
)
target_include_directories(lr2xhgr_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/lr2xhgr/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(lr2xhgr_dsp_test PRIVATE cxx_std_17)
add_test(NAME lr2xhgr_dsp_test COMMAND lr2xhgr_dsp_test)

add_executable(ltburst_dsp_test
    ltburst_dsp_test.cpp
)
target_include_directories(ltburst_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/ltburst/source
)
target_compile_features(ltburst_dsp_test PRIVATE cxx_std_17)
add_test(NAME ltburst_dsp_test COMMAND ltburst_dsp_test)

add_executable(ltglide_dsp_test
    ltglide_dsp_test.cpp
)
target_include_directories(ltglide_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/ltglide/source
)
target_compile_features(ltglide_dsp_test PRIVATE cxx_std_17)
add_test(NAME ltglide_dsp_test COMMAND ltglide_dsp_test)

add_executable(seam_meter_test
    seam_meter_test.cpp
)
target_include_directories(seam_meter_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_meter_test PRIVATE cxx_std_17)
add_test(NAME seam_meter_test COMMAND seam_meter_test)

add_executable(dslar_dsp_test
    dslar_dsp_test.cpp
)
target_include_directories(dslar_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/dslar/source
)
target_compile_features(dslar_dsp_test PRIVATE cxx_std_17)
add_test(NAME dslar_dsp_test COMMAND dslar_dsp_test)

add_executable(seam_fft_test
    seam_fft_test.cpp
)
target_include_directories(seam_fft_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_fft_test PRIVATE cxx_std_17)
add_test(NAME seam_fft_test COMMAND seam_fft_test)

add_executable(seam_rotation_test
    seam_rotation_test.cpp
)
target_include_directories(seam_rotation_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_rotation_test PRIVATE cxx_std_17)
add_test(NAME seam_rotation_test COMMAND seam_rotation_test)

add_executable(strx_dsp_test
    strx_dsp_test.cpp
)
target_include_directories(strx_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/strx/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(strx_dsp_test PRIVATE cxx_std_17)
add_test(NAME strx_dsp_test COMMAND strx_dsp_test)

add_executable(seam_calbus_test
    seam_calbus_test.cpp
)
target_include_directories(seam_calbus_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common/calbus
)
target_link_libraries(seam_calbus_test PRIVATE seam_calbus Threads::Threads)
target_compile_features(seam_calbus_test PRIVATE cxx_std_17)
add_test(NAME seam_calbus_test COMMAND seam_calbus_test)

add_executable(seam_calbus_client_test
    seam_calbus_client_test.cpp
)
target_include_directories(seam_calbus_client_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common/calbus
)
# The client loads the dylib at runtime — it must exist before the test runs.
add_dependencies(seam_calbus_client_test seam_calbus)
target_compile_features(seam_calbus_client_test PRIVATE cxx_std_17)
add_test(NAME seam_calbus_client_test COMMAND seam_calbus_client_test)
set_tests_properties(seam_calbus_client_test PROPERTIES
    ENVIRONMENT "SEAM_CALBUS_PATH=$<TARGET_FILE:seam_calbus>"
)

add_library(calbus_badversion_stub SHARED calbus_badversion_stub.cpp)
target_include_directories(calbus_badversion_stub PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common/calbus
)
target_compile_features(calbus_badversion_stub PRIVATE cxx_std_17)

add_executable(seam_calbus_version_test
    seam_calbus_version_test.cpp
)
target_include_directories(seam_calbus_version_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common/calbus
)
add_dependencies(seam_calbus_version_test calbus_badversion_stub)
target_compile_features(seam_calbus_version_test PRIVATE cxx_std_17)
add_test(NAME seam_calbus_version_test COMMAND seam_calbus_version_test)
set_tests_properties(seam_calbus_version_test PROPERTIES
    ENVIRONMENT "SEAM_CALBUS_PATH=$<TARGET_FILE:calbus_badversion_stub>"
)

add_executable(strx_calbus_digest_test
    strx_calbus_digest_test.cpp
)
target_include_directories(strx_calbus_digest_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/strx/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common/calbus
)
target_compile_features(strx_calbus_digest_test PRIVATE cxx_std_17)
add_test(NAME strx_calbus_digest_test COMMAND strx_calbus_digest_test)

add_executable(addelay_dsp_test
    addelay_dsp_test.cpp
)
target_include_directories(addelay_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/addelay/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(addelay_dsp_test PRIVATE cxx_std_17)
add_test(NAME addelay_dsp_test COMMAND addelay_dsp_test)

# ─── UI standard lint (see doc/style/ui-style.md) ────────────────────────────
# Two tests: the lint's own rule tests, then the lint over all plugins.
# Python3 is optional — on a machine without it the tests are skipped rather
# than failing the build, since no plugin needs Python to compile.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME uidesc_lint_selftest
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_check_uidesc.py)
    add_test(NAME uidesc_lint
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check-uidesc.py)
else()
    message(STATUS "Python3 not found — uidesc lint tests will not be registered")
endif()

# seam_state_test deliberately links the VST3 SDK base/common layers: the
# unit under test is the suite's IBStream decode rule itself (seam_state.h),
# exercised against SDK MemoryStream blobs including the legacy layouts.
add_executable(seam_state_test
    seam_state_test.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
)
target_include_directories(seam_state_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
    ${vst3sdk_SOURCE_DIR}
)
target_link_libraries(seam_state_test PRIVATE base sdk_common pluginterfaces)
if(APPLE)
    # CoreFoundation framework needed by base.a's fstring.o (Steinberg SDK)
    # Must be added via target_link_libraries to ensure correct link order
    find_library(CORE_FOUNDATION CoreFoundation)
    target_link_libraries(seam_state_test PRIVATE ${CORE_FOUNDATION})
endif()
target_compile_features(seam_state_test PRIVATE cxx_std_17)
add_test(NAME seam_state_test COMMAND seam_state_test)

add_executable(strx_bands_test
    strx_bands_test.cpp
)
target_include_directories(strx_bands_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/strx/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(strx_bands_test PRIVATE cxx_std_17)
add_test(NAME strx_bands_test COMMAND strx_bands_test)
