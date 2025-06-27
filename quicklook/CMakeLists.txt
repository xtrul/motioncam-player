set(QL_PREVIEW_TARGET MotionCamQLPreview)

set(QL_PREVIEW_SOURCES
    PreviewProvider.mm
)

add_library(${QL_PREVIEW_TARGET} MODULE ${QL_PREVIEW_SOURCES})
set_target_properties(${QL_PREVIEW_TARGET} PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "appex"
    XCODE_ATTRIBUTE_WRAPPER_EXTENSION "appex"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    FRAMEWORK FALSE
)
target_compile_options(${QL_PREVIEW_TARGET} PRIVATE -fobjc-arc)

target_include_directories(${QL_PREVIEW_TARGET} PRIVATE
    ${PROJECT_SOURCE_DIR}/motioncam-decoder/lib/include
    ${PROJECT_SOURCE_DIR}/motioncam-decoder/thirdparty
)

target_link_libraries(${QL_PREVIEW_TARGET} PRIVATE
    motioncam_decoder
    "-framework AppKit" "-framework QuickLook" "-framework QuickLookThumbnailing" "-framework QuickLookUI" "-framework ImageIO" "-framework UniformTypeIdentifiers"
)

set(APP_BUNDLE_ROOT_DIR "${CMAKE_BINARY_DIR}/${APP_BUNDLE_OUTPUT_NAME}.app")
add_custom_command(TARGET ${QL_PREVIEW_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_BUNDLE_ROOT_DIR}/Contents/Library/QuickLook"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:${QL_PREVIEW_TARGET}>" "${APP_BUNDLE_ROOT_DIR}/Contents/Library/QuickLook/${QL_PREVIEW_TARGET}.appex"
    COMMENT "Embedding Quick Look preview extension" VERBATIM
)
