add_executable(GpuYuvConverterTest GpuYuvConverterTest.cpp)
find_package(GTest CONFIG REQUIRED)
target_link_libraries(GpuYuvConverterTest PRIVATE GTest::gtest_main ${PROJECT_NAME})
add_test(NAME GpuYuvConverterTest COMMAND GpuYuvConverterTest)
