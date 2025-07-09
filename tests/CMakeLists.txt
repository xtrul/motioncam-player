find_package(GTest REQUIRED)
add_executable(gpu_converter_test gpu_converter_test.cpp)
target_link_libraries(gpu_converter_test PRIVATE GTest::gtest GTest::gtest_main)
