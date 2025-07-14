add_executable(test_dnxhr
    test_dnxhr.cpp
    ../src/Export/DnxhrExporter.cpp
    ../src/Utils/DebugLog.cpp
)
target_include_directories(test_dnxhr PRIVATE ../include ../include/Export ../include/Utils ${FFMPEG_INCLUDE_DIRS})
if(TARGET FFMPEG::avcodec)
  target_link_libraries(test_dnxhr PRIVATE FFMPEG::avcodec FFMPEG::avutil)
else()
  target_link_libraries(test_dnxhr PRIVATE ${FFMPEG_LIBRARIES})
endif()
add_test(NAME test_dnxhr COMMAND test_dnxhr)

add_executable(test_slice_writer
    test_slice_writer.cpp
    ../src/Export/SliceWriter.cpp
    ../src/Utils/DebugLog.cpp
)
target_include_directories(test_slice_writer PRIVATE ../include ../include/Export ../include/Utils)
add_test(NAME test_slice_writer COMMAND test_slice_writer)

add_executable(test_raw_dnxhr_writer
    test_raw_dnxhr_writer.cpp
    ../src/Export/SliceWriter.cpp
    ../src/Export/RawDnxhrWriter.cpp
    ../src/Utils/DebugLog.cpp
)
target_include_directories(test_raw_dnxhr_writer PRIVATE ../include ../include/Export ../include/Utils ${FFMPEG_INCLUDE_DIRS})
if(TARGET FFMPEG::avformat)
  target_link_libraries(test_raw_dnxhr_writer PRIVATE FFMPEG::avformat FFMPEG::avcodec FFMPEG::avutil)
else()
  target_link_libraries(test_raw_dnxhr_writer PRIVATE ${FFMPEG_LIBRARIES})
endif()
add_test(NAME test_raw_dnxhr_writer COMMAND test_raw_dnxhr_writer)
