# CPack packaging: .deb on Linux amd64, .dmg (DragNDrop) on Apple Silicon.
# Version comes solely from project(zwriter VERSION ...) → PROJECT_VERSION.
# Artifact names match zedit style:
#   zwriter-${PROJECT_VERSION}-Linux-amd64.deb
#   zwriter-${PROJECT_VERSION}-Darwin.dmg

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
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6 | libqt6widgets6t64")

  install(TARGETS zwriter RUNTIME DESTINATION bin COMPONENT zwriter)
  install(FILES ${CMAKE_SOURCE_DIR}/assets/linux/zwriter.desktop
          DESTINATION share/applications
          COMPONENT zwriter)
  install(FILES ${CMAKE_SOURCE_DIR}/assets/icons/zwriter-128.png
          DESTINATION share/icons/hicolor/128x128/apps
          RENAME zwriter.png
          COMPONENT zwriter)
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/
          DESTINATION share/zwriter/assets
          COMPONENT zwriter
          PATTERN "linux" EXCLUDE)
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
  set(CPACK_DMG_VOLUME_NAME "zwriter ${PROJECT_VERSION}")
  set(CPACK_PACKAGE_FILE_NAME "zwriter-${PROJECT_VERSION}-Darwin")
  # Binary at dmg root for now; full .app + macdeployqt can follow later.
  install(TARGETS zwriter RUNTIME DESTINATION . COMPONENT zwriter)
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/
          DESTINATION share/zwriter/assets
          COMPONENT zwriter
          PATTERN "linux" EXCLUDE)
endif()

include(CPack)
message(STATUS "zwriter: CPack configured (version ${PROJECT_VERSION})")
