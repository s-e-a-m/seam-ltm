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

    # ─── Minimum macOS the built binaries declare ────────────────────────────
    # Reads the bundles and the calibration bus as they come out of this build
    # tree, so the thing checked is the artefact a release ships and not the
    # CMake variable that was meant to shape it. See the deployment-target
    # block in the root CMakeLists.txt for what this catches.
    # A tests-only tree (SEAM_BUILD_PLUGINS=OFF) builds no bundle: its
    # VST3/<config> holds whatever an earlier configuration left there, and
    # checking those would judge stale artefacts, not this build.
    if(APPLE)
        if(SEAM_BUILD_PLUGINS)
            set(_minos_bundles ${CMAKE_BINARY_DIR}/VST3/$<CONFIG>)
        else()
            set(_minos_bundles "")
        endif()
        add_test(NAME minos_lint
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check-minos.py
                         ${_minos_bundles}
                         $<TARGET_FILE:seam_calbus>)
    endif()
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

add_executable(strx_export_test
    strx_export_test.cpp
)
target_include_directories(strx_export_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/strx/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(strx_export_test PRIVATE cxx_std_17)
add_test(NAME strx_export_test COMMAND strx_export_test)

add_executable(multipink_pink_test
    multipink_pink_test.cpp
)
target_include_directories(multipink_pink_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/multipink/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/strx/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(multipink_pink_test PRIVATE cxx_std_17)
add_test(NAME multipink_pink_test COMMAND multipink_pink_test)

add_executable(multipink_pink_engine_test
    multipink_pink_engine_test.cpp
)
target_include_directories(multipink_pink_engine_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/multipink/source
)
target_compile_features(multipink_pink_engine_test PRIVATE cxx_std_17)
add_test(NAME multipink_pink_engine_test COMMAND multipink_pink_engine_test)

# SSCDO#2 LMO: the reusable _common headers and the engine, against the Faust
# references in ref/lmo_ref.h (doc/study/sscdo2/lmo-plugin/gen-ref.sh).
add_executable(seam_noise_test seam_noise_test.cpp)
target_include_directories(seam_noise_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_noise_test PRIVATE cxx_std_17)
add_test(NAME seam_noise_test COMMAND seam_noise_test)

add_executable(seam_butterworth_test seam_butterworth_test.cpp)
target_include_directories(seam_butterworth_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_butterworth_test PRIVATE cxx_std_17)
add_test(NAME seam_butterworth_test COMMAND seam_butterworth_test)

add_executable(seam_ramp_test seam_ramp_test.cpp)
target_include_directories(seam_ramp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_ramp_test PRIVATE cxx_std_17)
add_test(NAME seam_ramp_test COMMAND seam_ramp_test)

add_executable(seam_basics_test seam_basics_test.cpp)
target_include_directories(seam_basics_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_basics_test PRIVATE cxx_std_17)
add_test(NAME seam_basics_test COMMAND seam_basics_test)

add_executable(seam_svf_test seam_svf_test.cpp)
target_include_directories(seam_svf_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_svf_test PRIVATE cxx_std_17)
add_test(NAME seam_svf_test COMMAND seam_svf_test)

add_executable(seam_analyzers_test seam_analyzers_test.cpp)
target_include_directories(seam_analyzers_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_analyzers_test PRIVATE cxx_std_17)
add_test(NAME seam_analyzers_test COMMAND seam_analyzers_test)

add_executable(lmo_dsp_test lmo_dsp_test.cpp)
target_include_directories(lmo_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/lmo/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(lmo_dsp_test PRIVATE cxx_std_17)
add_test(NAME lmo_dsp_test COMMAND lmo_dsp_test)

add_executable(choir_dsp_test choir_dsp_test.cpp)
target_include_directories(choir_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/choir/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(choir_dsp_test PRIVATE cxx_std_17)
add_test(NAME choir_dsp_test COMMAND choir_dsp_test)

add_executable(lmo_params_test lmo_params_test.cpp)
target_include_directories(lmo_params_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/lmo/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(lmo_params_test PRIVATE cxx_std_17)
add_test(NAME lmo_params_test COMMAND lmo_params_test)

# seam_primes.h: the sieve behind sff.np / sma.ms2npsamp (stunedrev, and any
# plugin with prime delays).
add_executable(seam_primes_test seam_primes_test.cpp)
target_include_directories(seam_primes_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_primes_test PRIVATE cxx_std_17)
add_test(NAME seam_primes_test COMMAND seam_primes_test)

# seam_moorer.h: Moorer's all-pass against sjm.apfv; references in
# ref/seam_moorer_ref.h (doc/study/sscdo2/stunedrev-plugin/gen-ref.sh).
add_executable(seam_moorer_test seam_moorer_test.cpp)
target_include_directories(seam_moorer_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_moorer_test PRIVATE cxx_std_17)
add_test(NAME seam_moorer_test COMMAND seam_moorer_test)

# stunedrev: the engine against sdt.stunedrev; references in
# ref/stunedrev_ref.h (doc/study/sscdo2/stunedrev-plugin/gen-ref.sh).
add_executable(stunedrev_dsp_test stunedrev_dsp_test.cpp)
target_include_directories(stunedrev_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/stunedrev/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(stunedrev_dsp_test PRIVATE cxx_std_17)
add_test(NAME stunedrev_dsp_test COMMAND stunedrev_dsp_test)

add_executable(stunedrev_params_test stunedrev_params_test.cpp)
target_include_directories(stunedrev_params_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/stunedrev/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(stunedrev_params_test PRIVATE cxx_std_17)
add_test(NAME stunedrev_params_test COMMAND stunedrev_params_test)

# stunedrev_state_test links the SDK base layers, as seam_state_test does:
# the unit under test is the processor's IBStream codec.
add_executable(stunedrev_state_test
    stunedrev_state_test.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
)
target_include_directories(stunedrev_state_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/stunedrev/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
    ${vst3sdk_SOURCE_DIR}
)
target_link_libraries(stunedrev_state_test PRIVATE base sdk_common pluginterfaces)
if(APPLE)
    find_library(CORE_FOUNDATION CoreFoundation)
    target_link_libraries(stunedrev_state_test PRIVATE ${CORE_FOUNDATION})
endif()
target_compile_features(stunedrev_state_test PRIVATE cxx_std_17)
add_test(NAME stunedrev_state_test COMMAND stunedrev_state_test)

# seam_delays.h: de.delay for an integer delay (delRM, and any plugin with a
# plain delay line).
add_executable(seam_delays_test seam_delays_test.cpp)
target_include_directories(seam_delays_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_delays_test PRIVATE cxx_std_17)
add_test(NAME seam_delays_test COMMAND seam_delays_test)

# seam_filters.h: sfi.leakyint; references in ref/seam_filters_ref.h
# (doc/study/sscdo2/delrm-plugin/gen-ref.sh).
add_executable(seam_filters_test seam_filters_test.cpp)
target_include_directories(seam_filters_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_filters_test PRIVATE cxx_std_17)
add_test(NAME seam_filters_test COMMAND seam_filters_test)

# seam_compressors.h: co.compressor_mono; references in
# ref/seam_compressors_ref.h (doc/study/sscdo2/delrm-plugin/gen-ref.sh).
add_executable(seam_compressors_test seam_compressors_test.cpp)
target_include_directories(seam_compressors_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(seam_compressors_test PRIVATE cxx_std_17)
add_test(NAME seam_compressors_test COMMAND seam_compressors_test)

# delrm: the engine against sdt.delrm*; references in ref/delrm_ref.h
# (doc/study/sscdo2/delrm-plugin/gen-ref.sh).
add_executable(delrm_dsp_test delrm_dsp_test.cpp)
target_include_directories(delrm_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/delrm/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(delrm_dsp_test PRIVATE cxx_std_17)
add_test(NAME delrm_dsp_test COMMAND delrm_dsp_test)

add_executable(delrm_params_test delrm_params_test.cpp)
target_include_directories(delrm_params_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/delrm/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(delrm_params_test PRIVATE cxx_std_17)
add_test(NAME delrm_params_test COMMAND delrm_params_test)

add_executable(delrm_state_test
    delrm_state_test.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
)
target_include_directories(delrm_state_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/delrm/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
    ${vst3sdk_SOURCE_DIR}
)
target_link_libraries(delrm_state_test PRIVATE base sdk_common pluginterfaces)
if(APPLE)
    target_link_libraries(delrm_state_test PRIVATE ${CORE_FOUNDATION})
endif()
target_compile_features(delrm_state_test PRIVATE cxx_std_17)
add_test(NAME delrm_state_test COMMAND delrm_state_test)

# delrm_footer.h: the footer's text, which must fit its view at every rate.
add_executable(delrm_footer_test delrm_footer_test.cpp)
target_include_directories(delrm_footer_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/delrm/source
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/_common
)
target_compile_features(delrm_footer_test PRIVATE cxx_std_17)
add_test(NAME delrm_footer_test COMMAND delrm_footer_test)
