cmake_minimum_required(VERSION 3.21)

# zwriter only supports Apple Silicon on macOS. This must be set before
# project() -- setting CMAKE_OSX_ARCHITECTURES any later is too late for the
# compiler checks project() runs. Guarded so an explicit
# -DCMAKE_OSX_ARCHITECTURES=... on the command line still wins.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures for macOS" FORCE)
endif()
# Minimum macOS for zwriter.app (Info.plist LSMinimumSystemVersion). CI links
# Homebrew Qt bottles built for macOS 14 (Sonoma), so nothing older can work.
set(ZWRITER_MACOS_MIN "14.0")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "${ZWRITER_MACOS_MIN}" CACHE STRING "Minimum macOS version" FORCE)
endif()

# Single version source of truth (same scheme as zedit). Bump ONLY here.
# Semver MAJOR.MINOR.PATCH; GitHub Release tags vX.Y.Z; CPack artifact
# names include PROJECT_VERSION (see cmake/Packaging.cmake).
project(zwriter VERSION 1.1.1 LANGUAGES CXX)

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
  src/InsertTableDialog.cpp
  src/InsertTableDialog.hpp
  src/HeaderFooterDialog.cpp
  src/HeaderFooterDialog.hpp
  src/SpellChecker.cpp
  src/SpellChecker.hpp
  src/SpellHighlighter.cpp
  src/SpellHighlighter.hpp
  src/PageWidgets.cpp
  src/PageWidgets.hpp
  src/Theme.cpp
  src/Theme.hpp
  src/icons.qrc
  src/ui.qrc
)

target_link_libraries(zwriter PRIVATE Qt6::Widgets Qt6::PrintSupport Qt6::Network)
target_include_directories(zwriter PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${ZWRITER_GENERATED_DIR}
)


if(TARGET Qt6::Multimedia)
  target_link_libraries(zwriter PRIVATE Qt6::Multimedia)
  target_compile_definitions(zwriter PRIVATE ZWRITER_HAS_MULTIMEDIA=1)
  # Embed the key samples so sounds work from any install location.
  target_sources(zwriter PRIVATE src/sounds.qrc)
  set_property(SOURCE src/sounds.qrc APPEND PROPERTY OBJECT_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/sounds/key-1.wav
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/sounds/key-2.wav
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/sounds/key-3.wav
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/sounds/key-4.wav
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/sounds/key-5.wav
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/sounds/key-6.wav
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/sounds/space.wav
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/sounds/return.wav)
  message(STATUS "zwriter: Qt6 Multimedia found — typewriter key sounds enabled")
else()
  message(STATUS "zwriter: Qt6 Multimedia not found — key-sound toggle stubs as no-op")
endif()

# Optional Hunspell spell-check (en_US system dictionary).
# Source uses #include <hunspell/hunspell.hxx>, so the include root must be the
# parent of the hunspell/ subdirectory (e.g. /usr/include, brew .../include).
# Upstream hunspell.pc sets Cflags: -I${includedir}/hunspell (for
# #include <hunspell.hxx>), which breaks our include style on Homebrew —
# do not wire PkgConfig::HUNSPELL's include dirs blindly.
set(_zwriter_hunspell_hints)
if(DEFINED ENV{HOMEBREW_PREFIX})
  list(APPEND _zwriter_hunspell_hints "$ENV{HOMEBREW_PREFIX}")
endif()
find_program(_ZWRITER_BREW_EXECUTABLE brew)
if(_ZWRITER_BREW_EXECUTABLE)
  execute_process(
    COMMAND "${_ZWRITER_BREW_EXECUTABLE}" --prefix hunspell
    OUTPUT_VARIABLE _zwriter_brew_hunspell
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(_zwriter_brew_hunspell)
    list(APPEND _zwriter_hunspell_hints "${_zwriter_brew_hunspell}")
  endif()
endif()
list(APPEND _zwriter_hunspell_hints /opt/homebrew /usr/local /usr)

find_path(HUNSPELL_INCLUDE_DIR
  NAMES hunspell/hunspell.hxx
  HINTS ${_zwriter_hunspell_hints}
  PATH_SUFFIXES include
)
find_library(HUNSPELL_LIBRARY
  NAMES hunspell-1.7 hunspell
  HINTS ${_zwriter_hunspell_hints}
  PATH_SUFFIXES lib
)

# pkg-config: use for library discovery / path normalization only.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(HUNSPELL_PC QUIET hunspell)
  if(HUNSPELL_PC_FOUND)
    if(NOT HUNSPELL_LIBRARY)
      if(HUNSPELL_PC_LINK_LIBRARIES)
        set(HUNSPELL_LIBRARY ${HUNSPELL_PC_LINK_LIBRARIES})
      else()
        set(HUNSPELL_LIBRARY ${HUNSPELL_PC_LIBRARIES})
      endif()
    endif()
    if(NOT HUNSPELL_INCLUDE_DIR AND HUNSPELL_PC_INCLUDE_DIRS)
      foreach(_inc IN LISTS HUNSPELL_PC_INCLUDE_DIRS)
        get_filename_component(_tail "${_inc}" NAME)
        if(_tail STREQUAL "hunspell")
          get_filename_component(_parent "${_inc}" DIRECTORY)
          if(EXISTS "${_parent}/hunspell/hunspell.hxx")
            set(HUNSPELL_INCLUDE_DIR "${_parent}")
            break()
          endif()
        elseif(EXISTS "${_inc}/hunspell/hunspell.hxx")
          set(HUNSPELL_INCLUDE_DIR "${_inc}")
          break()
        endif()
      endforeach()
    endif()
  endif()
endif()

if(HUNSPELL_INCLUDE_DIR AND HUNSPELL_LIBRARY)
  target_include_directories(zwriter PRIVATE ${HUNSPELL_INCLUDE_DIR})
  target_link_libraries(zwriter PRIVATE ${HUNSPELL_LIBRARY})
  if(HUNSPELL_PC_LIBRARY_DIRS)
    target_link_directories(zwriter PRIVATE ${HUNSPELL_PC_LIBRARY_DIRS})
  endif()
  target_compile_definitions(zwriter PRIVATE ZWRITER_HAS_HUNSPELL=1)
  message(STATUS "zwriter: Hunspell found — spell check enabled (${HUNSPELL_INCLUDE_DIR})")
else()
  message(STATUS "zwriter: Hunspell not found — spell check stubs as no-op")
endif()

# macOS: zwriter.app bundle (Info.plist, icon, bundled dictionary/sounds,
# macdeployqt at install time).
if(APPLE)
  include(cmake/MacBundle.cmake)
endif()

# Headless ODT round-trip regression test (ctest). Needs unzip + zip at run time.
option(ZWRITER_BUILD_TESTS "Build the zwriter regression tests" ON)
if(ZWRITER_BUILD_TESTS)
  enable_testing()
  find_package(Qt6 REQUIRED COMPONENTS Gui)
  add_executable(odt_roundtrip_test
    tests/odt_roundtrip_test.cpp
    src/DocumentIo.cpp
    src/DocumentIo.hpp
  )
  target_include_directories(odt_roundtrip_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(odt_roundtrip_test PRIVATE Qt6::Gui)
  add_test(NAME odt_roundtrip COMMAND odt_roundtrip_test)
  set_tests_properties(odt_roundtrip PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Packaging (.deb / .dmg) — owns install() rules + CPack.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Packaging.cmake")
  include(cmake/Packaging.cmake)
endif()
