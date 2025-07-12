find_package(PkgConfig REQUIRED)

pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswscale libswresample)

set(FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES})
set(FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIRS})
set(FFmpeg_FOUND TRUE)
set(FFMPEG_FOUND TRUE)
