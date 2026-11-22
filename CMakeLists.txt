cmake_minimum_required(VERSION 3.24)

project(NekoPad
    VERSION 0.1.0
    DESCRIPTION "A fast, cross-platform text and source code editor"
    LANGUAGES CXX
)

option(NEKOPAD_BUILD_TESTS "Build the Neko Pad test suites" ON)
option(NEKOPAD_LTO "Link-time optimization in optimized builds" ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/NekoPadCpuArch.cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Speed is the point: LTO across our code and the NekoControls libraries in Release and
# RelWithDebInfo, where the toolchain supports it.
if(NEKOPAD_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _nekopad_ipo OUTPUT _nekopad_ipo_error LANGUAGES CXX)
    if(_nekopad_ipo)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
        message(STATUS "Link-time optimization: on in optimized builds")
    else()
        message(STATUS "Link-time optimization: not supported (${_nekopad_ipo_error})")
    endif()
endif()

# No Qt WebEngine: Neko Pad starts without Chromium, which is most of its start-up speed.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Svg Network)

# ---------------------------------------------------------------------------
# NekoControls: the text editor (neko::editor -- CodeEditor, EditorFindBar,
# MarkdownHighlighter) and the look of the Neko applications (neko::theme). Pinned to a
# release's tarball -- an immutable release asset -- by URL and SHA-256.
#
# Only those two libraries are added, not NekoControls' top-level CMakeLists.txt: that
# one requires Qt WebEngine and downloads the Markdown preview's JavaScript, neither of
# which Neko Pad uses. SOURCE_SUBDIR names a directory without a CMakeLists.txt, so
# FetchContent_MakeAvailable() only downloads.
#
# To work on NekoControls and Neko Pad together, configure with
#     -DFETCHCONTENT_SOURCE_DIR_NEKO_CONTROLS=/abs/path/to/NekoControls
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    neko_controls
    URL      https://github.com/fhuber83/NekoControls/releases/download/v0.6.0/NekoControls-0.6.0.tar.gz
    URL_HASH SHA256=8b6c6c4c7284a9912630b974eabb3c10c81af1b8ba11723fee05b36dc563335f
    SOURCE_SUBDIR _nekopad_libraries_only
)
FetchContent_MakeAvailable(neko_controls)
add_subdirectory(${neko_controls_SOURCE_DIR}/src/neko/editor ${neko_controls_BINARY_DIR}/editor)
add_subdirectory(${neko_controls_SOURCE_DIR}/src/neko/theme ${neko_controls_BINARY_DIR}/theme)
include(${neko_controls_SOURCE_DIR}/cmake/NekoIconSet.cmake)

# ---------------------------------------------------------------------------
# nekopad_core: encodings, line endings, file loading and saving, the language table and
# the line operations. Qt Core only -- test_core links it alone, so an include of ui/
# from src/core fails to link there.
# ---------------------------------------------------------------------------
add_library(nekopad_core STATIC
    src/core/text_encoding.hpp
    src/core/text_encoding.cpp
    src/core/text_file.hpp
    src/core/text_file.cpp
    src/core/language.hpp
    src/core/language.cpp
    src/core/line_ops.hpp
    src/core/line_ops.cpp
    src/core/backup_store.hpp
    src/core/backup_store.cpp
)
target_include_directories(nekopad_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nekopad_core PUBLIC Qt6::Core)

# ---------------------------------------------------------------------------
# nekopad_ui: the main window, its tabs around neko::editor::CodeEditor, the syntax
# highlighter, the single-instance link.
# ---------------------------------------------------------------------------
add_library(nekopad_ui STATIC
    src/ui/app_icon.hpp
    src/ui/app_icon.cpp
    src/ui/syntax_highlighter.hpp
    src/ui/syntax_highlighter.cpp
    src/ui/editor_tab.hpp
    src/ui/editor_tab.cpp
    src/ui/main_window.hpp
    src/ui/main_window.cpp
    src/ui/single_instance.hpp
    src/ui/single_instance.cpp
    src/ui/settings.hpp
    src/ui/settings.cpp
    resources/nekopad.qrc
)
target_include_directories(nekopad_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nekopad_ui PUBLIC
    nekopad_core
    neko::editor
    neko::theme
    Qt6::Widgets
    Qt6::Network
)
target_compile_definitions(nekopad_ui PUBLIC NEKOPAD_VERSION_STRING="${PROJECT_VERSION}")

# UI icons (neko::theme::themedIcon): resources/icons.txt maps the names the code asks
# for to Lucide files; neko_add_icon_set() compiles them in, with the controls' own.
neko_add_icon_set(nekopad_ui MAP ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons.txt)

set(NEKOPAD_EXTRA_SOURCES)
if(WIN32)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/nekopad.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/nekopad.rc
        @ONLY
    )
    list(APPEND NEKOPAD_EXTRA_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/nekopad.rc)
endif()

add_executable(nekopad WIN32 MACOSX_BUNDLE src/main.cpp ${NEKOPAD_EXTRA_SOURCES})
target_link_libraries(nekopad PRIVATE nekopad_ui)
set_target_properties(nekopad PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Neko Pad"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.fhuber83.nekopad"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

foreach(_target nekopad_core nekopad_ui nekopad)
    target_compile_definitions(${_target} PRIVATE QT_NO_FOREACH QT_NO_URL_CAST_FROM_STRING)
    if(MSVC)
        target_compile_options(${_target} PRIVATE /utf-8 /permissive- /W4)
    else()
        target_compile_options(${_target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS nekopad RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES resources/nekopad.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES resources/nekopad.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
elseif(WIN32 OR APPLE)
    install(TARGETS nekopad RUNTIME DESTINATION . BUNDLE DESTINATION .)
endif()

# Windows: deploy the Qt offscreen platform plugin next to the binaries, for test_ui (as
# Neko Note does it): windeployqt and vcpkg copy only platforms/qwindows.dll.
if(WIN32 AND TARGET Qt6::QOffscreenIntegrationPlugin)
    get_property(_nekopad_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(_nekopad_win_out_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    else()
        set(_nekopad_win_out_dir "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    if(_nekopad_multi_config)
        string(APPEND _nekopad_win_out_dir "/$<CONFIG>")
    endif()
    add_custom_target(nekopad_win_plugins
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_nekopad_win_out_dir}/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>
            "${_nekopad_win_out_dir}/platforms/"
        COMMENT "Deploying the Qt offscreen platform plugin"
        VERBATIM
    )
    add_dependencies(nekopad nekopad_win_plugins)
endif()

if(NEKOPAD_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()

    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE nekopad_core Qt6::Test)
    add_test(NAME test_core COMMAND test_core)

    add_executable(test_ui tests/test_ui.cpp)
    target_link_libraries(test_ui PRIVATE nekopad_ui Qt6::Test)
    add_test(NAME test_ui COMMAND test_ui)
    set_tests_properties(test_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(TARGET nekopad_win_plugins)
        add_dependencies(test_ui nekopad_win_plugins)
    endif()

    # A benchmark, not a test: how long loading and showing a large file takes.
    add_executable(bench_load tests/bench_load.cpp)
    target_link_libraries(bench_load PRIVATE nekopad_ui Qt6::Test)
endif()
