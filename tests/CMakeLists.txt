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
)
target_compile_features(x2uhj_dsp_test PRIVATE cxx_std_17)
add_test(NAME x2uhj_dsp_test COMMAND x2uhj_dsp_test)
