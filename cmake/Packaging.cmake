# CPack packaging: .deb on Linux amd64, .dmg (DragNDrop) on Apple Silicon.
# Version comes solely from project(zwriter VERSION ...) → PROJECT_VERSION.
# Artifact names match zedit style:
#   zwriter-${PROJECT_VERSION}-Linux-amd64.deb
#   zwriter-${PROJECT_VERSION}-Darwin.dmg  (zwriter.app + /Applications link)

set(CPACK_PACKAGE_NAME "zwriter")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Stephen B. Johnson")
set(CPACK_PACKAGE_CONTACT "Stephen B. Johnson <49662809+sbj-ee@users.noreply.github.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/sbj-ee/zwriter")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Distraction-free writing for Linux amd64 and Apple Silicon")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

set(CPACK_DEB_COMPONENT_INSTALL YES)
set(CPACK_COMPONENTS_ALL zwriter)
set(CPACK_DEBIAN_ZWRITER_PACKAGE_NAME "zwriter")
set(CPACK_DEBIAN_ZWRITER_FILE_NAME "zwriter-${PROJECT_VERSION}-Linux-amd64.deb")

if(UNIX AND NOT APPLE)
  set(CPACK_GENERATOR "DEB")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
  set(CPACK_DEBIAN_PACKAGE_SECTION "editors")
  set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  # Fallback depends if shlibdeps unavailable on a runner.
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6 | libqt6widgets6t64, libhunspell-1.7-0, hunspell-en-us")

  install(TARGETS zwriter RUNTIME DESTINATION bin COMPONENT zwriter)
  # Short command name: `zw` next to `zwriter` on $PATH. A relative symlink, so
  # it survives DESTDIR staging and a prefix move. Safe to run under either
  # name: main.cpp sets the application, organisation and desktop-file names
  # explicitly, so QSettings, the window title and the dock icon do not depend
  # on argv[0].
  install(CODE [[
    set(_zw_bindir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin")
    file(MAKE_DIRECTORY "${_zw_bindir}")
    file(CREATE_LINK zwriter "${_zw_bindir}/zw" SYMBOLIC)
    message(STATUS "Symlinked ${_zw_bindir}/zw -> zwriter")
  ]] COMPONENT zwriter)
  install(FILES ${CMAKE_SOURCE_DIR}/assets/linux/zwriter.desktop
          DESTINATION share/applications
          COMPONENT zwriter)
  foreach(_sz 16 24 32 48 64 128 256 512)
    install(FILES ${CMAKE_SOURCE_DIR}/assets/icons/zwriter-${_sz}.png
            DESTINATION share/icons/hicolor/${_sz}x${_sz}/apps
            RENAME zwriter.png
            COMPONENT zwriter)
  endforeach()
  install(FILES ${CMAKE_SOURCE_DIR}/assets/icons/zwriter.svg
          DESTINATION share/icons/hicolor/scalable/apps
          COMPONENT zwriter)
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/
          DESTINATION share/zwriter/assets
          COMPONENT zwriter
          PATTERN "linux" EXCLUDE)
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
  set(CPACK_DMG_VOLUME_NAME "zwriter ${PROJECT_VERSION}")
  set(CPACK_PACKAGE_FILE_NAME "zwriter-${PROJECT_VERSION}-Darwin")
  # No license-agreement prompt when the dmg is mounted (CMake >= 3.23); the
  # licence is in the repository and the About box.
  set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE OFF)
  # zwriter.app at the dmg root next to CPack's /Applications symlink.
  # MacDeploy.cmake (from cmake/MacBundle.cmake) then runs macdeployqt on the
  # staged app, removes non-bundle rpaths, ad-hoc signs and checks it.
  install(TARGETS zwriter BUNDLE DESTINATION . COMPONENT zwriter)
  install(SCRIPT "${CMAKE_BINARY_DIR}/MacDeploy.cmake" COMPONENT zwriter)
endif()

include(CPack)
message(STATUS "zwriter: CPack configured (version ${PROJECT_VERSION})")
