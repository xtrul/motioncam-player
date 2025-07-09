add_executable(UnitTests GpuYuvConverterTest.cpp)
find_package(GTest REQUIRED)
target_link_libraries(UnitTests PRIVATE ${GTEST_BOTH_LIBRARIES} pthread
    motioncam_decoder imgui glm Vulkan::Vulkan)

add_test(NAME UnitTests COMMAND UnitTests)
