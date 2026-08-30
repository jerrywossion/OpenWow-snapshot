add_executable(openwow-ios-texture-cache
  main.cpp
)
target_include_directories(openwow-ios-texture-cache PRIVATE
  ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(openwow-ios-texture-cache PRIVATE
  openwow_data_runtime
  openwow_vfs_runtime
  bgfx::bimg
  bgfx::bimg_encode
  bgfx::bx
)
openwow_configure_target(openwow-ios-texture-cache)

add_custom_target(openwow-ios-prepare-data
  COMMAND "$<TARGET_FILE:openwow-ios-texture-cache>"
    --game-root "${OPENWOW_LOCAL_CONTENT_ROOT}"
    --output "${OPENWOW_LOCAL_CONTENT_ROOT}/Data/OpenWoWDerived/iOS/Textures"
    --locale "${OPENWOW_IOS_TEXTURE_CACHE_LOCALE}"
    --enhanced-assets-root "${PROJECT_SOURCE_DIR}/assets/overrides"
  DEPENDS openwow-ios-texture-cache
  COMMENT "Incrementally generate the iOS ASTC texture cache"
  USES_TERMINAL
  VERBATIM)
