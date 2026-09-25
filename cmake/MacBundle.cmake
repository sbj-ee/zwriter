# macOS: build zwriter as a real zwriter.app bundle (Apple Silicon only).
# Included from the top-level CMakeLists.txt after the zwriter target exists.
#   - Info.plist from cmake/Info.plist.in (versions from PROJECT_VERSION)
#   - zwriter.icns generated from assets/icons/*.png with iconutil
#   - Contents/Resources: hunspell/en_US.{aff,dic}, assets/sounds, assets/icons
# Qt frameworks/plugins and libhunspell are copied in at install time by
# macdeployqt (cmake/MacDeploy.cmake.in, run by `cpack -G DragNDrop`).

set(ZWRITER_BUNDLE_ID "ee.sbj.zwriter")

set_target_properties(zwriter PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
  MACOSX_BUNDLE_GUI_IDENTIFIER "${ZWRITER_BUNDLE_ID}"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

# --- App icon: iconset from the existing PNG art -> zwriter.icns ----------
find_program(ZWRITER_ICONUTIL iconutil REQUIRED)
set(_icons "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons")
set(_iconset "${CMAKE_CURRENT_BINARY_DIR}/zwriter.iconset")
set(_icns "${CMAKE_CURRENT_BINARY_DIR}/zwriter.icns")
# iconset name -> source PNG size (no 1024 px art exists; 512@2x is omitted).
set(_iconmap
  icon_16x16:16 icon_16x16@2x:32 icon_32x32:32 icon_32x32@2x:64
  icon_128x128:128 icon_128x128@2x:256 icon_256x256:256 icon_256x256@2x:512
  icon_512x512:512)
set(_copy_cmds)
set(_icon_deps)
foreach(_pair IN LISTS _iconmap)
  string(REPLACE ":" ";" _kv "${_pair}")
  list(GET _kv 0 _name)
  list(GET _kv 1 _size)
  list(APPEND _copy_cmds COMMAND ${CMAKE_COMMAND} -E copy
       "${_icons}/zwriter-${_size}.png" "${_iconset}/${_name}.png")
  list(APPEND _icon_deps "${_icons}/zwriter-${_size}.png")
endforeach()
list(REMOVE_DUPLICATES _icon_deps)
add_custom_command(
  OUTPUT "${_icns}"
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${_iconset}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_iconset}"
  ${_copy_cmds}
  COMMAND "${ZWRITER_ICONUTIL}" -c icns "${_iconset}" -o "${_icns}"
  DEPENDS ${_icon_deps}
  COMMENT "Generating zwriter.icns"
  VERBATIM)
target_sources(zwriter PRIVATE "${_icns}")
set_source_files_properties("${_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# --- Bundled resources -----------------------------------------------------
set(_dicts
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/hunspell-en_US/en_US.aff"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/hunspell-en_US/en_US.dic")
file(GLOB _sounds "${CMAKE_CURRENT_SOURCE_DIR}/assets/sounds/*.wav")
target_sources(zwriter PRIVATE ${_dicts} ${_sounds})
set_source_files_properties(${_dicts} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/hunspell)
set_source_files_properties(${_sounds} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/assets/sounds)

# --- Deployment (runs at install time, i.e. inside `cpack -G DragNDrop`) ---
get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qtbin "${_qmake}" DIRECTORY)
find_program(ZWRITER_MACDEPLOYQT NAMES macdeployqt macdeployqt6
  HINTS "${_qtbin}" REQUIRED)
set(ZWRITER_HUNSPELL_LIBDIR "")
if(HUNSPELL_LIBRARY)
  list(GET HUNSPELL_LIBRARY 0 _hl)
  if(IS_ABSOLUTE "${_hl}")
    get_filename_component(ZWRITER_HUNSPELL_LIBDIR "${_hl}" DIRECTORY)
  endif()
endif()
set(ZWRITER_CHECK_BUNDLE "${CMAKE_CURRENT_SOURCE_DIR}/tools/macos/check-bundle.sh")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacDeploy.cmake.in"
               "${CMAKE_CURRENT_BINARY_DIR}/MacDeploy.cmake" @ONLY)
message(STATUS "zwriter: macOS app bundle ${ZWRITER_BUNDLE_ID}, macdeployqt ${ZWRITER_MACDEPLOYQT}")
