# SEAM-LTM unit tests (doctest, SDK-free DSP cores)
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

add_executable(ltburst_dsp_test
    ltburst_dsp_test.cpp
)
target_include_directories(ltburst_dsp_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../plugins/ltburst/source
)
target_compile_features(ltburst_dsp_test PRIVATE cxx_std_17)
add_test(NAME ltburst_dsp_test COMMAND ltburst_dsp_test)
