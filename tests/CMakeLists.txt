add_executable(quadbayer_test QuadBayerDemosaicTest.cpp)
target_include_directories(quadbayer_test PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_sources(quadbayer_test PRIVATE
    ${PROJECT_SOURCE_DIR}/src/Utils/QuadBayerDemosaic.cpp
    ${PROJECT_SOURCE_DIR}/src/Utils/ColorPipelineCPU.cpp)
add_test(NAME QuadBayerTest COMMAND quadbayer_test)
