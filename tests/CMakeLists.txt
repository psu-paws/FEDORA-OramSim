include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library( test_util OBJECT
    "test_util.cpp"
)

target_link_libraries(
    test_util
    absl::random_random
    absl::strings
    absl::str_format
    GTest::gtest 
    GTest::gmock 
    GTest::gtest_main
    OramLibrary
)
function(add_gtest_test test_name)
    add_executable(${test_name} "${test_name}.cpp" ${oram_simulator_SRC})
    target_link_libraries(
        ${test_name} 
        test_util 
        GTest::gtest 
        GTest::gmock 
        GTest::gtest_main
        OramLibrary
    )
    gtest_discover_tests(
        ${test_name}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    set_target_properties(${test_name} PROPERTIES FOLDER tests)
endfunction()

add_gtest_test(test_util_test)
add_gtest_test(simple_memory_test)