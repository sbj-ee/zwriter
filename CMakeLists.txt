cmake_minimum_required(VERSION 3.21)

# zwriter only supports Apple Silicon on macOS. This must be set before
# project() -- setting CMAKE_OSX_ARCHITECTURES any later is too late for the
# compiler checks project() runs. Guarded so an explicit
# -DCMAKE_OSX_ARCHITECTURES=... on the command line still wins.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for macOS" FORCE)
endif()

# Single version source of truth (same scheme as zedit). Bump ONLY here.
# Semver MAJOR.MINOR.PATCH; GitHub Release tags vX.Y.Z; CPack artifact
# names include PROJECT_VERSION (see cmake/Packaging.cmake).
project(zwriter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport Network)
find_package(Qt6 COMPONENTS Multimedia QUIET)

# Generated version header from project() VERSION — do not edit by hand.
set(ZWRITER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${ZWRITER_GENERATED_DIR}")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/version.hpp.in"
  "${ZWRITER_GENERATED_DIR}/version.hpp"
  @ONLY
)

add_executable(zwriter
  src/main.cpp
  src/MainWindow.cpp
  src/MainWindow.hpp
  src/DocumentIo.cpp
  src/DocumentIo.hpp
  src/DocumentMeta.cpp
  src/DocumentMeta.hpp
  src/PropertiesDialog.cpp
  src/PropertiesDialog.hpp
  src/TypewriterSounds.cpp
  src/TypewriterSounds.hpp
  src/FindReplaceBar.cpp
  src/FindReplaceBar.hpp
  src/UpdateChecker.cpp
  src/UpdateChecker.hpp
)

target_link_libraries(zwriter PRIVATE Qt6::Widgets Qt6::PrintSupport Qt6::Network)
target_include_directories(zwriter PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${ZWRITER_GENERATED_DIR}
)

if(APPLE)
  set_target_properties(zwriter PROPERTIES
    MACOSX_BUNDLE FALSE
  )
endif()

if(TARGET Qt6::Multimedia)
  target_link_libraries(zwriter PRIVATE Qt6::Multimedia)
  target_compile_definitions(zwriter PRIVATE ZWRITER_HAS_MULTIMEDIA=1)
  message(STATUS "zwriter: Qt6 Multimedia found — typewriter key sounds enabled")
else()
  message(STATUS "zwriter: Qt6 Multimedia not found — key-sound toggle stubs as no-op")
endif()

# Packaging (.deb / .dmg) — owns install() rules + CPack.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Packaging.cmake")
  include(cmake/Packaging.cmake)
endif()
