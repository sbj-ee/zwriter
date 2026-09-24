# Packaging stub for zwriter (.deb on Linux, .dmg on Apple Silicon).
#
# Not wired into a working CPack config yet. Including this file must not
# break configure or build. When ready:
#
#   Linux .deb:
#     set(CPACK_GENERATOR "DEB")
#     set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Stephen B. Johnson <stevebj.ee@gmail.com>")
#     set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6")
#     set(CPACK_PACKAGE_CONTACT "stevebj.ee@gmail.com")
#     include(CPack)
#
#   macOS arm64 .dmg:
#     set(CPACK_GENERATOR "DragNDrop")
#     set(CPACK_DMG_VOLUME_NAME "zwriter")
#     # Bundle Qt frameworks via macdeployqt after build.
#     include(CPack)
#
# Platforms in scope: Linux amd64 + Apple Silicon only (no Windows, no Intel Mac).

message(STATUS "zwriter: Packaging.cmake stub loaded (CPack not configured yet)")
