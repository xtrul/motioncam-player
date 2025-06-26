cmake_minimum_required(VERSION 3.10)
project(mcraw_thumbnail)
set(CMAKE_CXX_STANDARD 17)

add_subdirectory(../motioncam-decoder motioncam_decoder_build EXCLUDE_FROM_ALL)

add_executable(mcraw_thumbnail mcraw_thumbnail.cpp)

target_include_directories(mcraw_thumbnail PRIVATE
    ../motioncam-decoder/lib/include
    ../motioncam-decoder/thirdparty
    .)

target_link_libraries(mcraw_thumbnail PRIVATE motioncam_decoder)
