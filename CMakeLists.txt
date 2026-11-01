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
project(zwriter VERSION 1.18.3 LANGUAGES CXX)

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

# libsodium: Ed25519 + BLAKE2b to verify the minisign signature on a release's
# SHA256SUMS before the in-app updater installs anything (src/Minisign.cpp).
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium>=1.0.18)

# Short git commit baked into the build (title bar: "zwriter <ver> (<commit>)").
# Order: -DZWRITER_GIT_COMMIT=..., then $ZWRITER_GIT_COMMIT, then
# `git rev-parse --short=7 HEAD` of this checkout, else "unknown" (a source
# tarball). Resolved at configure time; re-run cmake after new commits.
if(NOT ZWRITER_GIT_COMMIT AND DEFINED ENV{ZWRITER_GIT_COMMIT})
  set(ZWRITER_GIT_COMMIT "$ENV{ZWRITER_GIT_COMMIT}")
endif()
if(NOT ZWRITER_GIT_COMMIT)
  find_package(Git QUIET)
  if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE ZWRITER_GIT_COMMIT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    # Re-configure when HEAD moves so the commit does not go stale.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD")
  endif()
endif()
if(NOT ZWRITER_GIT_COMMIT)
  set(ZWRITER_GIT_COMMIT "unknown")
endif()
message(STATUS "zwriter ${PROJECT_VERSION} (${ZWRITER_GIT_COMMIT})")

# Generated version header from project() VERSION — do not edit by hand.
set(ZWRITER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${ZWRITER_GENERATED_DIR}")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/version.hpp.in"
  "${ZWRITER_GENERATED_DIR}/version.hpp"
  @ONLY
)

# The in-app updater (check, download, verify, install, restart prompt), a
# library so the tests link exactly what the app runs.
# Warnings for zwriter's own targets (all targets below; vendored
# third_party/miniz has them cleared).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra)
endif()

add_library(zwriter_update STATIC
  src/Minisign.cpp
  src/Minisign.hpp
  src/Update.cpp
  src/Update.hpp
  src/UpdateChecker.cpp
  src/UpdateChecker.hpp
  src/UpdateDialog.cpp
  src/UpdateDialog.hpp
  src/UpdateHost.hpp
  src/UpdateManager.cpp
  src/UpdateManager.hpp
  src/UpdateSigningKey.cpp
)
target_include_directories(zwriter_update PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${ZWRITER_GENERATED_DIR}
)
target_link_libraries(zwriter_update PUBLIC Qt6::Widgets Qt6::Network PkgConfig::SODIUM)

# In-process zip (vendored miniz) and the XML tree used to patch ODF parts:
# no `zip` / `unzip` child processes. Linked into the app and every test.
enable_language(C)
add_library(miniz STATIC third_party/miniz/miniz.c)
target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz)
set_target_properties(miniz PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
# Vendored code is built without the project's warning flags.
set_property(TARGET miniz PROPERTY COMPILE_OPTIONS "")
add_library(zwriter_odf STATIC src/ZipPackage.cpp src/ZipPackage.hpp src/OdfXml.cpp src/OdfXml.hpp)
target_include_directories(zwriter_odf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(zwriter_odf PUBLIC Qt6::Core PRIVATE miniz)
link_libraries(zwriter_odf)

add_executable(zwriter
  src/main.cpp
  src/MainWindow.cpp
  src/MainWindow.hpp
  src/DocumentIo.cpp
  src/Images.cpp
  src/ParagraphStyles.cpp
  src/StyleFonts.cpp
  src/StylesDialog.cpp
  src/StylesDialog.hpp
  src/ParagraphDialog.cpp
  src/Ruler.cpp
  src/PageSetup.cpp
  src/PageSetup.hpp
  src/PageSettingsDialog.cpp
  src/PageSettingsDialog.hpp
  src/Symbols.cpp
  src/Symbols.hpp
  src/SymbolDialog.cpp
  src/SymbolDialog.hpp
  src/Images.hpp
  src/DocumentIo.hpp
  src/DocumentMeta.cpp
  src/DocumentMeta.hpp
  src/PropertiesDialog.cpp
  src/PropertiesDialog.hpp
  src/TypewriterSounds.cpp
  src/TypewriterSounds.hpp
  src/FindReplaceBar.cpp
  src/FindReplaceBar.hpp
  src/InsertTableDialog.cpp
  src/InsertTableDialog.hpp
  src/ImagePropertiesDialog.cpp
  src/ImagePropertiesDialog.hpp
  src/HeaderFooterDialog.cpp
  src/HeaderFooterDialog.hpp
  src/SpellChecker.cpp
  src/SpellChecker.hpp
  src/WritingHighlighter.cpp
  src/WritingHighlighter.hpp
  src/MarkdownSyntax.cpp
  src/MarkdownSyntax.hpp
  src/FocusRange.cpp
  src/FocusRange.hpp
  src/PageWidgets.cpp
  src/TableColumns.cpp
  src/TableColumns.hpp
  src/PageWidgets.hpp
  src/PrintLayout.cpp
  src/PrintLayout.hpp
  src/Theme.cpp
  src/Theme.hpp
  src/Alignment.cpp
  src/Alignment.hpp
  src/AlignmentActions.cpp
  src/AlignmentActions.hpp
  src/PreviewPane.cpp
  src/PreviewPane.hpp
  src/LibrarySidebar.cpp
  src/LibrarySidebar.hpp
  src/BookManifest.cpp
  src/BookManifest.hpp
  src/BookChapterDialog.cpp
  src/BookChapterDialog.hpp
  src/BookSidebar.cpp
  src/BookToc.cpp
  src/BookToc.hpp
  src/Captions.cpp
  src/BookExport.cpp
  src/BookExport.hpp
  src/BookExportDialog.cpp
  src/BookExportDialog.hpp
  src/Captions.hpp
  src/BookSidebar.hpp
  src/icons.qrc
  src/ui.qrc
  src/fonts.qrc
)

target_link_libraries(zwriter PRIVATE Qt6::Widgets Qt6::PrintSupport Qt6::Network zwriter_update)
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

# Development-only environment overrides (ZWRITER_UPDATE_URL points the update
# check at a local test server). On by default only for Debug builds; release
# packages never read them.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_zw_dev_overrides_default ON)
else()
  set(_zw_dev_overrides_default OFF)
endif()
option(ZWRITER_DEV_OVERRIDES "Honour development environment overrides such as ZWRITER_UPDATE_URL (debug builds only)"
       ${_zw_dev_overrides_default})
if(ZWRITER_DEV_OVERRIDES)
  target_compile_definitions(zwriter_update PUBLIC ZWRITER_DEV_OVERRIDES)
  message(STATUS "zwriter: ZWRITER_DEV_OVERRIDES=ON (environment overrides compiled in; not for release packages)")
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
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/Images.hpp
    src/DocumentIo.hpp
    src/Alignment.cpp
    src/Alignment.hpp
  )
  target_include_directories(odt_roundtrip_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(odt_roundtrip_test PRIVATE Qt6::Gui)
  add_test(NAME odt_roundtrip COMMAND odt_roundtrip_test)
  set_tests_properties(odt_roundtrip PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Images in documents: ODT pictures round trip, LibreOffice-shaped frames.
  add_executable(images_test
    tests/images_test.cpp
    src/DocumentIo.cpp
    src/DocumentIo.hpp
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/Images.hpp
    src/Alignment.cpp
    src/Alignment.hpp
  )
  target_include_directories(images_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(images_test PRIVATE Qt6::Gui)
  add_test(NAME images COMMAND images_test)
  set_tests_properties(images PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(paragraph_test
    tests/paragraph_test.cpp
    src/DocumentIo.cpp
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/ParagraphDialog.cpp
    src/Alignment.cpp
  )
  target_include_directories(paragraph_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(paragraph_test PRIVATE Qt6::Widgets)
  add_test(NAME paragraph COMMAND paragraph_test)
  set_tests_properties(paragraph PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(toc_preserve_test
    tests/toc_preserve_test.cpp
    src/DocumentIo.cpp
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/Alignment.cpp
  )
  target_include_directories(toc_preserve_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(toc_preserve_test PRIVATE ZW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(toc_preserve_test PRIVATE Qt6::Gui)
  add_test(NAME toc_preserve COMMAND toc_preserve_test)
  set_tests_properties(toc_preserve PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(book_toc_test
    tests/book_toc_test.cpp
    src/BookToc.cpp
    src/BookManifest.cpp
    src/DocumentIo.cpp
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/Alignment.cpp
  )
  target_include_directories(book_toc_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(book_toc_test PRIVATE ZW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(book_toc_test PRIVATE Qt6::Gui)
  add_test(NAME book_toc COMMAND book_toc_test)
  set_tests_properties(book_toc PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(book_export_test
    tests/book_export_test.cpp
    src/BookExport.cpp
    src/BookManifest.cpp
    src/Captions.cpp
    src/DocumentIo.cpp
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/Alignment.cpp
    src/PrintLayout.cpp
  )
  target_include_directories(book_export_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(book_export_test PRIVATE Qt6::Widgets Qt6::PrintSupport)
  add_test(NAME book_export COMMAND book_export_test)
  set_tests_properties(book_export PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Tables (1.16.0): column widths, merged cells and header rows through ODT
  # (incl. LibreOffice-style widths) and EPUB; drag-resize geometry.
  add_executable(table_test
    tests/table_test.cpp
    src/TableColumns.cpp
    src/BookExport.cpp
    src/BookManifest.cpp
    src/Captions.cpp
    src/DocumentIo.cpp
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/Alignment.cpp
    src/PrintLayout.cpp
  )
  target_include_directories(table_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(table_test PRIVATE Qt6::Widgets Qt6::PrintSupport)
  add_test(NAME table COMMAND table_test)
  set_tests_properties(table PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Subscript / superscript (1.13.0): ODT/RTF round trips, PDF layout, EPUB <sub>/<sup>.
  add_executable(script_test
    tests/script_test.cpp
    src/BookExport.cpp
    src/BookManifest.cpp
    src/Captions.cpp
    src/DocumentIo.cpp
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/Alignment.cpp
    src/PrintLayout.cpp
  )
  target_include_directories(script_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(script_test PRIVATE Qt6::Widgets Qt6::PrintSupport)
  add_test(NAME script COMMAND script_test)
  set_tests_properties(script PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(captions_test
    tests/captions_test.cpp
    src/Captions.cpp
    src/DocumentIo.cpp
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/Alignment.cpp
  )
  target_include_directories(captions_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(captions_test PRIVATE Qt6::Gui)
  add_test(NAME captions COMMAND captions_test)
  set_tests_properties(captions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # GUI-test regressions: print scale, ODT manifest/styles, RTF import/export, tables.
  add_executable(regression_test
    tests/regression_test.cpp
    src/DocumentIo.cpp
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/Images.hpp
    src/DocumentIo.hpp
    src/Alignment.cpp
    src/Alignment.hpp
    src/DocumentMeta.cpp
    src/DocumentMeta.hpp
    src/PrintLayout.cpp
    src/PrintLayout.hpp
  )
  target_include_directories(regression_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${ZWRITER_GENERATED_DIR})
  target_link_libraries(regression_test PRIVATE Qt6::Gui)
  add_test(NAME regression COMMAND regression_test)
  set_tests_properties(regression PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Atomic save (1.17.1): temp file + rename, .bak, failed patch steps fail the save.
  add_executable(atomic_save_test
    tests/atomic_save_test.cpp
    src/DocumentIo.cpp
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/Alignment.cpp
    src/DocumentMeta.cpp
    src/PrintLayout.cpp
  )
  target_include_directories(atomic_save_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${ZWRITER_GENERATED_DIR})
  target_link_libraries(atomic_save_test PRIVATE Qt6::Gui)
  add_test(NAME atomic_save COMMAND atomic_save_test)
  set_tests_properties(atomic_save PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(odt_package_test
    tests/odt_package_test.cpp
    src/DocumentIo.cpp
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/Alignment.cpp
    src/DocumentMeta.cpp
    src/PrintLayout.cpp
  )
  target_include_directories(odt_package_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${ZWRITER_GENERATED_DIR})
  target_link_libraries(odt_package_test PRIVATE Qt6::Gui)
  add_test(NAME odt_package COMMAND odt_package_test)
  set_tests_properties(odt_package PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Style fonts (1.15.0): per-style font family and size, settings, ODT meta, restyle, CSS.
  add_executable(stylefonts_test
    tests/stylefonts_test.cpp
    src/DocumentIo.cpp
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/Alignment.cpp
    src/DocumentMeta.cpp
  )
  target_include_directories(stylefonts_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${ZWRITER_GENERATED_DIR})
  target_link_libraries(stylefonts_test PRIVATE Qt6::Gui)
  add_test(NAME stylefonts COMMAND stylefonts_test)
  set_tests_properties(stylefonts PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Paragraph alignment: ODT/RTF round-trip, one-step undo, checked state, print.
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
  # Symbols (1.13.0): \name table, autocomplete and its undo, Recently used.
  add_executable(symbols_test tests/symbols_test.cpp src/Symbols.cpp src/Symbols.hpp)
  target_include_directories(symbols_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(symbols_test PRIVATE Qt6::Widgets)
  add_test(NAME symbols COMMAND symbols_test)
  set_tests_properties(symbols PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(alignment_test
    tests/alignment_test.cpp
    src/Alignment.cpp
    src/Alignment.hpp
    src/AlignmentActions.cpp
    src/AlignmentActions.hpp
    src/DocumentIo.cpp
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/Images.hpp
    src/DocumentIo.hpp
    src/PrintLayout.cpp
    src/PrintLayout.hpp
  )
  target_include_directories(alignment_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(alignment_test PRIVATE Qt6::Widgets)
  add_test(NAME alignment COMMAND alignment_test)
  set_tests_properties(alignment PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_STYLE_OVERRIDE=Fusion")

  # Markdown mode: byte-faithful round-trip (open, edit, save), the Markdown
  # scanner / highlighter, and focus-mode sentence boundaries.
  add_executable(markdown_test
    tests/markdown_test.cpp
    src/DocumentIo.cpp
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/Images.hpp
    src/DocumentIo.hpp
    src/Alignment.cpp
    src/Alignment.hpp
    src/MarkdownSyntax.cpp
    src/MarkdownSyntax.hpp
    src/WritingHighlighter.cpp
    src/WritingHighlighter.hpp
    src/FocusRange.cpp
    src/FocusRange.hpp
    src/Theme.cpp
    src/Theme.hpp
    src/fonts.qrc
  )
  target_include_directories(markdown_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(markdown_test PRIVATE Qt6::Gui)
  add_test(NAME markdown COMMAND markdown_test)
  set_tests_properties(markdown PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  # Markdown preview pane (debounce, render, theme, scroll sync) and library
  # sidebar (file filter, click-to-open), plus their QSettings keys.
  add_executable(panes_test
    tests/panes_test.cpp
    src/PreviewPane.cpp
    src/PreviewPane.hpp
    src/LibrarySidebar.cpp
    src/LibrarySidebar.hpp
    src/Theme.cpp
    src/Theme.hpp
  )
  target_include_directories(panes_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(panes_test PRIVATE Qt6::Widgets)
  add_test(NAME panes COMMAND panes_test)
  set_tests_properties(panes PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_STYLE_OVERRIDE=Fusion")

  # Updater: version compare, release JSON, checksum, minisign (fixtures made
  # with the real minisign tool and a throwaway key), and the whole flow
  # (check -> dialog -> download -> verify -> install -> restart prompt)
  # against a mock GitHub on a local socket. No network, no apt, no sudo.
  find_package(Qt6 REQUIRED COMPONENTS Test)
  foreach(_t minisign_test update_test)
    add_executable(${_t} tests/${_t}.cpp)
    target_link_libraries(${_t} PRIVATE zwriter_update Qt6::Test)
    target_compile_definitions(${_t} PRIVATE
      ZWRITER_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
      ZWRITER_EXPECTED_VERSION="${PROJECT_VERSION}")
    add_test(NAME ${_t} COMMAND ${_t})
    set_tests_properties(${_t} PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_STYLE_OVERRIDE=Fusion;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config-${_t}"
      TIMEOUT 120)
  endforeach()
endif()

# Library folder chooser, empty-library hint, bounded behaviour on a huge
# library, and the "zwriter <ver> (<commit>)" label.
if(TARGET Qt6::Widgets)
  add_executable(library_test
    tests/library_test.cpp
    src/LibrarySidebar.cpp
    src/LibrarySidebar.hpp
    src/Theme.cpp
    src/Theme.hpp
  )
  target_include_directories(library_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${ZWRITER_GENERATED_DIR})
  target_link_libraries(library_test PRIVATE Qt6::Widgets)
  add_test(NAME library COMMAND library_test)
  set_tests_properties(library PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_STYLE_OVERRIDE=Fusion")
endif()

# Book sidebar (1.4.0): book.json load/save, tolerant loading, missing files,
# reorder persistence, metadata round-trip, remove keeps files.
if(TARGET Qt6::Widgets)
  add_executable(book_test
    tests/book_test.cpp
    src/BookManifest.cpp
    src/BookManifest.hpp
    src/BookChapterDialog.cpp
    src/BookChapterDialog.hpp
    src/BookSidebar.cpp
    src/BookSidebar.hpp
    src/DocumentIo.cpp
    src/DocumentIo.hpp
    src/Images.cpp
    src/ParagraphStyles.cpp
    src/StyleFonts.cpp
    src/Images.hpp
    src/Alignment.cpp
    src/Alignment.hpp
    src/Theme.cpp
    src/Theme.hpp
    src/ui.qrc
  )
  target_include_directories(book_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${ZWRITER_GENERATED_DIR})
  find_package(Qt6 REQUIRED COMPONENTS Test)
  target_link_libraries(book_test PRIVATE Qt6::Widgets Qt6::Test)
  add_test(NAME book COMMAND book_test)
  add_test(NAME deb_depends COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/deb_depends_test.cmake)
  set_tests_properties(book PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_STYLE_OVERRIDE=Fusion;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/config-book_test")
endif()

# Whole-window tests: the real MainWindow, offscreen. Built from the same
# sources and settings as the zwriter target (minus main.cpp), so it can never
# drift from the shipped binary.
if(TARGET Qt6::Widgets)
  get_target_property(_zw_srcs zwriter SOURCES)
  list(FILTER _zw_srcs EXCLUDE REGEX "(^|/)main\\.cpp$")
  add_executable(mainwindow_test tests/mainwindow_test.cpp ${_zw_srcs})
  foreach(_prop COMPILE_DEFINITIONS INCLUDE_DIRECTORIES LINK_LIBRARIES LINK_DIRECTORIES)
    get_target_property(_val zwriter ${_prop})
    if(_val)
      set_property(TARGET mainwindow_test PROPERTY ${_prop} ${_val})
    endif()
  endforeach()
  find_package(Qt6 REQUIRED COMPONENTS Test)
  target_link_libraries(mainwindow_test PRIVATE Qt6::Test)
  target_compile_definitions(mainwindow_test PRIVATE ZW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME mainwindow COMMAND mainwindow_test)
  set_tests_properties(mainwindow PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_STYLE_OVERRIDE=Fusion"
    TIMEOUT 300)
endif()

# Packaging (.deb / .dmg) — owns install() rules + CPack.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Packaging.cmake")
  include(cmake/Packaging.cmake)
endif()
