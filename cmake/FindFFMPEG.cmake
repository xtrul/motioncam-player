find_package(PkgConfig)

foreach(_comp avcodec avformat avutil swscale swresample)
    pkg_check_modules(PKG_${_comp} QUIET lib${_comp})
    if(PKG_${_comp}_FOUND)
        set(FFMPEG_${_comp}_LIBRARIES ${PKG_${_comp}_LINK_LIBRARIES})
        set(FFMPEG_${_comp}_INCLUDE_DIRS ${PKG_${_comp}_INCLUDE_DIRS})
    else()
        set(FFMPEG_FOUND FALSE)
        return()
    endif()
endforeach()

set(FFMPEG_LIBRARIES 
    ${FFMPEG_avcodec_LIBRARIES} ${FFMPEG_avformat_LIBRARIES} 
    ${FFMPEG_avutil_LIBRARIES} ${FFMPEG_swscale_LIBRARIES} 
    ${FFMPEG_swresample_LIBRARIES})
set(FFMPEG_INCLUDE_DIRS 
    ${FFMPEG_avcodec_INCLUDE_DIRS} ${FFMPEG_avformat_INCLUDE_DIRS} 
    ${FFMPEG_avutil_INCLUDE_DIRS} ${FFMPEG_swscale_INCLUDE_DIRS} 
    ${FFMPEG_swresample_INCLUDE_DIRS})
set(FFMPEG_FOUND TRUE)
