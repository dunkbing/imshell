cmake_minimum_required(VERSION 3.23)
project(imshell LANGUAGES CXX)

# dear imgui and the libraries on it are submodules in external/ (git submodule update --init
# --recursive); a host may set these to its own copies. the host still compiles imgui core once,
# from IMSHELL_IMGUI_DIR: a second copy of imgui's statics would silently split the context
set(IMSHELL_EXTERNAL ${CMAKE_CURRENT_SOURCE_DIR}/external)
set(IMSHELL_IMGUI_DIR "" CACHE PATH "Dear ImGui source folder (imgui.h, imconfig.h)")
if(NOT IMSHELL_IMGUI_DIR)
    set(IMSHELL_IMGUI_DIR ${IMSHELL_EXTERNAL}/imgui CACHE PATH "Dear ImGui source folder (imgui.h, imconfig.h)" FORCE)
endif()
if(NOT EXISTS ${IMSHELL_IMGUI_DIR}/imgui.h)
    message(FATAL_ERROR "imshell: no imgui.h in ${IMSHELL_IMGUI_DIR}; run git submodule update --init --recursive, or set IMSHELL_IMGUI_DIR")
endif()
# the test engine's sources, for a host to compile with its imgui (IMGUI_ENABLE_TEST_ENGINE)
set(IMSHELL_TEST_ENGINE_DIR ${IMSHELL_EXTERNAL}/imgui_test_engine/imgui_test_engine CACHE PATH "imgui_test_engine sources")
set(IMSHELL_NODE_EDITOR_DIR ${IMSHELL_EXTERNAL}/imgui-node-editor CACHE PATH "imgui-node-editor sources, for imshell::node_editor")

add_library(imshell STATIC
    src/activity.cpp
    src/alert.cpp
    src/clipboard.cpp
    src/app.cpp
    src/data_dir.cpp
    src/button.cpp
    src/embedded.cpp
    src/fonts.cpp
    src/frame_skip.cpp
    src/ime.cpp
    src/image.cpp
    src/select.cpp
    src/resizable.cpp
    src/spinner.cpp
    src/toggle_button.cpp
)
add_library(imshell::imshell ALIAS imshell)

# 32-bit characters in imgui: emoji and other text past U+FFFF (16-bit ImWchar shows them as
# "?"). it changes imgui's structs, so every file that includes imgui.h must see it: the
# host's own imgui sources get it through linking imshell
option(IMSHELL_WCHAR32 "Build Dear ImGui with IMGUI_USE_WCHAR32 (emoji past U+FFFF)" OFF)
if(IMSHELL_WCHAR32)
    target_compile_definitions(imshell PUBLIC IMGUI_USE_WCHAR32)
    add_compile_definitions(IMGUI_USE_WCHAR32) # imshell's own targets that do not link it (tests)
endif()

# colour emoji merged into AddFont's font: coretext on apple; directwrite with Segoe UI Emoji on
# windows; freetype with the system's colour font (Noto Color Emoji) on linux and android when
# freetype is found; none on the web yet
target_sources(imshell PRIVATE src/emoji_glyph.cpp)
if(APPLE)
    target_sources(imshell PRIVATE src/apple/emoji.mm)
    set_source_files_properties(src/apple/emoji.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(imshell PUBLIC "-framework CoreText")
elseif(WIN32)
    target_sources(imshell PRIVATE src/windows/emoji.cpp)
    target_link_libraries(imshell PRIVATE dwrite d2d1)
elseif((ANDROID OR UNIX) AND NOT EMSCRIPTEN)
    find_package(Freetype QUIET)
    if(FREETYPE_FOUND)
        target_sources(imshell PRIVATE src/emoji_freetype.cpp)
        target_link_libraries(imshell PRIVATE Freetype::Freetype)
    else()
        message(STATUS "imshell: no freetype, so no colour emoji")
        target_sources(imshell PRIVATE src/emoji_none.cpp)
    endif()
else()
    target_sources(imshell PRIVATE src/emoji_none.cpp)
endif()

# optional components (imshell/components.hpp): a host links imshell::components to use them
add_library(imshell_components STATIC src/components.cpp)
add_library(imshell::components ALIAS imshell_components)
target_link_libraries(imshell_components PUBLIC imshell)

# optional code editor (imshell/editor.hpp): a host links imshell::editor to use it
add_library(imshell_editor STATIC src/editor.cpp)
add_library(imshell::editor ALIAS imshell_editor)
target_link_libraries(imshell_editor PUBLIC imshell)

# optional markdown (imshell/markdown.hpp) through imgui_rich_md: the submodule, another folder in
# IMSHELL_RICH_MD_DIR, or the host's own imgui_rich_md target added before imshell
set(IMSHELL_RICH_MD_DIR ${IMSHELL_EXTERNAL}/imgui_rich_md CACHE PATH "imgui_rich_md source folder, for imshell::markdown")
if(EXISTS ${IMSHELL_RICH_MD_DIR}/CMakeLists.txt AND NOT TARGET imgui_rich_md)
    # it links a target named imgui; the host compiles imgui, so headers only
    if(NOT TARGET imgui)
        add_library(imgui INTERFACE)
        target_include_directories(imgui INTERFACE ${IMSHELL_IMGUI_DIR})
    endif()
    set(IMGUI_RICHMD_STB_IMAGE_IMPLEMENTATION OFF CACHE BOOL "" FORCE) # src/image.cpp has it
    add_subdirectory(${IMSHELL_RICH_MD_DIR} ${CMAKE_CURRENT_BINARY_DIR}/imgui_rich_md EXCLUDE_FROM_ALL)
endif()
if(TARGET imgui_rich_md)
    add_library(imshell_markdown STATIC src/markdown.cpp)
    add_library(imshell::markdown ALIAS imshell_markdown)
    target_link_libraries(imshell_markdown PUBLIC imshell_editor PRIVATE imgui_rich_md)
    if(IMSHELL_WCHAR32)
        target_compile_definitions(imgui_rich_md PUBLIC IMGUI_USE_WCHAR32)
    endif()
endif()

# optional node editor (imshell/node_editor.hpp): imgui-node-editor, themed
if(EXISTS ${IMSHELL_NODE_EDITOR_DIR}/imgui_node_editor.cpp)
    add_library(imshell_node_editor STATIC src/node_editor.cpp
        ${IMSHELL_NODE_EDITOR_DIR}/imgui_node_editor.cpp
        ${IMSHELL_NODE_EDITOR_DIR}/imgui_node_editor_api.cpp
        ${IMSHELL_NODE_EDITOR_DIR}/crude_json.cpp
        ${IMSHELL_NODE_EDITOR_DIR}/imgui_canvas.cpp)
    add_library(imshell::node_editor ALIAS imshell_node_editor)
    target_include_directories(imshell_node_editor PUBLIC ${IMSHELL_NODE_EDITOR_DIR})
    target_link_libraries(imshell_node_editor PUBLIC imshell)
endif()

# the platform shell. imgui core stays the host's; its platform backends are ours
if(EMSCRIPTEN) # before UNIX, which emscripten's toolchain also sets
    target_sources(imshell PRIVATE
        src/web/app.cpp
        src/haptics_none.cpp
        src/web/notifications.cpp
        src/native_view.cpp
        ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    # glfw on a canvas (emscripten's contrib port: hidpi, resizing, clipboard), webgl2
    target_compile_options(imshell PUBLIC --use-port=contrib.glfw3)
    target_link_options(imshell PUBLIC --use-port=contrib.glfw3 -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2
                                      -sALLOW_MEMORY_GROWTH=1)
elseif(IOS)
    target_sources(imshell PRIVATE src/ios/app.mm src/ios/file_dialog.mm src/ios/clipboard.mm src/apple/haptics.mm src/apple/native_view.mm src/native_view.cpp src/touch.cpp src/apple/notifications.mm
                                  ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_metal.mm)
    set_source_files_properties(src/ios/app.mm src/ios/file_dialog.mm src/ios/clipboard.mm src/apple/haptics.mm src/apple/native_view.mm src/apple/notifications.mm
                                PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(imshell PUBLIC "-framework UIKit" "-framework Metal" "-framework MetalKit"
        "-framework QuartzCore" "-framework Foundation" "-framework UniformTypeIdentifiers"
        "-framework UserNotifications")
elseif(APPLE)
    find_package(glfw3 CONFIG REQUIRED)
    target_sources(imshell PRIVATE
        src/macos/app.mm
        src/macos/metal_backend.mm
        src/macos/native_button.mm
        src/macos/clipboard.mm
        src/apple/notifications.mm
        src/apple/haptics.mm
        src/apple/native_view.mm
        src/native_view.cpp
        ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_metal.mm
    )
    # arc for both: they share MetalBackend, whose id members must be owned one way
    set_source_files_properties(src/macos/app.mm src/macos/metal_backend.mm src/macos/native_button.mm
                                src/macos/clipboard.mm src/apple/notifications.mm src/apple/haptics.mm src/apple/native_view.mm
                                PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(imshell PUBLIC glfw
        "-framework Metal" "-framework QuartzCore" "-framework AppKit" "-framework Foundation"
        "-framework ImageIO" "-framework CoreGraphics" "-framework UniformTypeIdentifiers"
        "-framework UserNotifications")
elseif(WIN32)
    find_package(glfw3 CONFIG REQUIRED)
    target_sources(imshell PRIVATE
        src/windows/app.cpp
        src/windows/clipboard.cpp
        src/windows/dx11_backend.cpp
        src/windows/native_view.cpp
        src/haptics_none.cpp
        src/notifications_none.cpp
        src/native_view.cpp
        ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_compile_definitions(imshell PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(imshell PUBLIC glfw d3d11 dxgi dwmapi shell32 shlwapi ole32 windowscodecs)
elseif(ANDROID)
    # NativeActivity through the ndk's glue; its entry point must survive static linking
    enable_language(C)
    set(IMSHELL_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
    target_sources(imshell PRIVATE
        src/android/app.cpp
        src/android/file_dialog.cpp
        src/android/notifications.cpp
        src/touch.cpp
        src/native_view.cpp
        ${IMSHELL_GLUE_DIR}/android_native_app_glue.c
        ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_android.cpp
        ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imshell PUBLIC ${IMSHELL_GLUE_DIR})
    target_link_libraries(imshell PUBLIC android EGL GLESv3 log)
    target_link_options(imshell INTERFACE -u ANativeActivity_onCreate)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ImShellAndroid.cmake)
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(IMSHELL_GTK4 REQUIRED IMPORTED_TARGET gtk4)
    pkg_check_modules(IMSHELL_EPOXY REQUIRED IMPORTED_TARGET epoxy)
    target_sources(imshell PRIVATE
        src/linux/app.cpp
        src/linux/clipboard.cpp
        src/linux/notifications.cpp
        src/haptics_none.cpp
        src/native_view.cpp
        ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_link_libraries(imshell PUBLIC PkgConfig::IMSHELL_GTK4 PkgConfig::IMSHELL_EPOXY)
endif()

# desktop file dialogs: native file dialog extended (vcpkg nativefiledialog-extended)
if(NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(nfd CONFIG REQUIRED)
    target_sources(imshell PRIVATE src/file_dialog_nfd.cpp)
    target_link_libraries(imshell PUBLIC nfd::nfd)
endif()

# auto-update (imshell/updater.hpp). sparkle (macos) and winsparkle (windows) are downloaded at
# configure time unless the host brings its own (IMSHELL_SPARKLE_FRAMEWORK, a WinSparkle target);
# imshell_app() (at the end) ships them with the app. on linux imshell finds httplib, nlohmann_json and openssl
option(IMSHELL_UPDATER "auto-update through sparkle, winsparkle or an appimage swap" OFF)
set(IMSHELL_SPARKLE_FRAMEWORK "" CACHE PATH "macos: Sparkle.framework (empty: downloaded)")
if(NOT IMSHELL_UPDATER OR IOS OR ANDROID OR EMSCRIPTEN)
    target_sources(imshell PRIVATE src/updater_none.cpp)
elseif(APPLE)
    if(NOT IMSHELL_SPARKLE_FRAMEWORK)
        include(FetchContent)
        FetchContent_Declare(imshell_sparkle
            URL https://github.com/sparkle-project/Sparkle/releases/download/2.9.1/Sparkle-2.9.1.tar.xz
            URL_HASH SHA256=c0dde519fd2a43ddfc6a1eb76aec284d7d888fe281414f9177de3164d98ba4c7
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(imshell_sparkle)
        set(IMSHELL_SPARKLE_FRAMEWORK ${imshell_sparkle_SOURCE_DIR}/Sparkle.framework CACHE PATH "" FORCE)
    endif()
    target_sources(imshell PRIVATE src/macos/updater.mm)
    set_source_files_properties(src/macos/updater.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_include_directories(imshell PRIVATE "${IMSHELL_SPARKLE_FRAMEWORK}/Headers")
    target_link_libraries(imshell PUBLIC "${IMSHELL_SPARKLE_FRAMEWORK}")
elseif(WIN32)
    if(NOT TARGET WinSparkle)
        include(FetchContent)
        FetchContent_Declare(imshell_winsparkle
            URL https://github.com/vslavik/winsparkle/releases/download/v0.9.2/WinSparkle-0.9.2.zip
            URL_HASH SHA256=2a5facf5a22056edf7afc4d0a4bf05620b7aa7cc1619c3078b281e374cdb9996
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(imshell_winsparkle)
        add_library(WinSparkle SHARED IMPORTED GLOBAL)
        set_target_properties(WinSparkle PROPERTIES
            IMPORTED_IMPLIB ${imshell_winsparkle_SOURCE_DIR}/x64/Release/WinSparkle.lib
            IMPORTED_LOCATION ${imshell_winsparkle_SOURCE_DIR}/x64/Release/WinSparkle.dll
            INTERFACE_INCLUDE_DIRECTORIES ${imshell_winsparkle_SOURCE_DIR}/include)
    endif()
    target_sources(imshell PRIVATE src/windows/updater.cpp)
    target_link_libraries(imshell PUBLIC WinSparkle)
else()
    find_package(httplib CONFIG REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
    find_package(OpenSSL REQUIRED)
    target_sources(imshell PRIVATE src/linux/updater.cpp)
    target_link_libraries(imshell PRIVATE httplib::httplib nlohmann_json::nlohmann_json
                                         OpenSSL::SSL OpenSSL::Crypto)
endif()

target_compile_features(imshell PUBLIC cxx_std_20)
target_include_directories(imshell PUBLIC include ${IMSHELL_IMGUI_DIR} ${IMSHELL_IMGUI_DIR}/backends
                                  PRIVATE third_party)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ImShellEmbed.cmake)

# examples are hosts too: each compiles its own imgui core
option(IMSHELL_BUILD_EXAMPLES "Build the examples" OFF)
if(IMSHELL_BUILD_EXAMPLES AND NOT IOS)
    file(GLOB IMSHELL_IMGUI_CORE ${IMSHELL_IMGUI_DIR}/imgui*.cpp)
    if(ANDROID)
        add_library(imshell_hello SHARED examples/hello/main.cpp examples/hello/native_android.cpp ${IMSHELL_IMGUI_CORE})
        imshell_android_apk(imshell_hello PACKAGE io.imshell.hello LABEL "Hello imshell"
                            PERMISSIONS android.permission.POST_NOTIFICATIONS)
    elseif(EMSCRIPTEN) # hello.html (+ .js, .wasm); serve the folder
        add_executable(imshell_hello examples/hello/main.cpp examples/hello/native_web.cpp ${IMSHELL_IMGUI_CORE})
        set_target_properties(imshell_hello PROPERTIES SUFFIX .html)
        target_link_options(imshell_hello PRIVATE --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/examples/web_shell.html)
    elseif(APPLE)
        add_executable(imshell_hello examples/hello/main.cpp examples/hello/native_macos.mm ${IMSHELL_IMGUI_CORE})
        set_source_files_properties(examples/hello/native_macos.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    elseif(WIN32)
        add_executable(imshell_hello examples/hello/main.cpp examples/hello/native_windows.cpp ${IMSHELL_IMGUI_CORE})
    else()
        add_executable(imshell_hello examples/hello/main.cpp examples/hello/native_linux.cpp ${IMSHELL_IMGUI_CORE})
    endif()
    target_link_libraries(imshell_hello PRIVATE imshell)
    # the components gallery; on the web it is the site's demo
    if(NOT ANDROID)
        add_executable(imshell_gallery examples/gallery/main.cpp ${IMSHELL_IMGUI_CORE})
        target_link_libraries(imshell_gallery PRIVATE imshell_components imshell_editor)
        if(EMSCRIPTEN)
            set_target_properties(imshell_gallery PROPERTIES SUFFIX .html)
            target_link_options(imshell_gallery PRIVATE --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/examples/web_shell.html)
        endif()
    endif()
endif()

# ReadText and WriteText through imgui's hooks where the shell fills them (and Read and Write
# on the web, text only)
if(NOT APPLE)
    target_sources(imshell PRIVATE src/clipboard_text.cpp)
endif()

# the touch engine's test: a bare imgui context, no platform
option(IMSHELL_BUILD_TESTS "Build the tests" OFF)
if(IMSHELL_BUILD_TESTS)
    enable_testing()
    file(GLOB IMSHELL_TEST_IMGUI ${IMSHELL_IMGUI_DIR}/imgui*.cpp)
    add_executable(imshell_touch_test tests/touch_test.cpp src/touch.cpp ${IMSHELL_TEST_IMGUI})
    target_include_directories(imshell_touch_test PRIVATE src ${IMSHELL_IMGUI_DIR})
    target_compile_features(imshell_touch_test PRIVATE cxx_std_20)
    add_test(NAME imshell_touch_test COMMAND imshell_touch_test)
    if(APPLE AND NOT IOS)
        add_executable(imshell_clipboard_test tests/clipboard_test.mm src/macos/clipboard.mm src/clipboard.cpp)
        set_source_files_properties(tests/clipboard_test.mm src/macos/clipboard.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
        target_include_directories(imshell_clipboard_test PRIVATE include)
        target_compile_features(imshell_clipboard_test PRIVATE cxx_std_20)
        target_link_libraries(imshell_clipboard_test PRIVATE "-framework AppKit" "-framework UniformTypeIdentifiers")
        add_test(NAME imshell_clipboard_test COMMAND imshell_clipboard_test)
    elseif(IOS) # run in the simulator: xcrun simctl spawn booted imshell_clipboard_test
        add_executable(imshell_clipboard_test tests/clipboard_test_ios.mm src/ios/clipboard.mm src/clipboard.cpp)
        set_source_files_properties(tests/clipboard_test_ios.mm src/ios/clipboard.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
        target_include_directories(imshell_clipboard_test PRIVATE include)
        target_compile_features(imshell_clipboard_test PRIVATE cxx_std_20)
        target_link_libraries(imshell_clipboard_test PRIVATE "-framework UIKit" "-framework UniformTypeIdentifiers")
    elseif(WIN32) # replaces what is on the clipboard
        add_executable(imshell_clipboard_test tests/clipboard_test_win.cpp src/windows/clipboard.cpp src/clipboard.cpp)
        target_include_directories(imshell_clipboard_test PRIVATE include)
        target_compile_features(imshell_clipboard_test PRIVATE cxx_std_20)
        target_compile_definitions(imshell_clipboard_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(imshell_clipboard_test PRIVATE shell32 shlwapi ole32 windowscodecs)
        add_test(NAME imshell_clipboard_test COMMAND imshell_clipboard_test)
    elseif(UNIX AND NOT ANDROID AND NOT EMSCRIPTEN) # needs a display (xvfb-run); replaces what is on the clipboard
        add_executable(imshell_clipboard_test tests/clipboard_test_gtk.cpp src/linux/clipboard.cpp src/clipboard.cpp)
        target_include_directories(imshell_clipboard_test PRIVATE include)
        target_compile_features(imshell_clipboard_test PRIVATE cxx_std_20)
        target_link_libraries(imshell_clipboard_test PRIVATE PkgConfig::IMSHELL_GTK4)
        add_test(NAME imshell_clipboard_test COMMAND imshell_clipboard_test)
    endif()
    add_executable(imshell_frame_skip_test tests/frame_skip_test.cpp src/frame_skip.cpp ${IMSHELL_TEST_IMGUI})
    target_include_directories(imshell_frame_skip_test PRIVATE src ${IMSHELL_IMGUI_DIR})
    target_compile_features(imshell_frame_skip_test PRIVATE cxx_std_20)
    add_test(NAME imshell_frame_skip_test COMMAND imshell_frame_skip_test)
    add_executable(imshell_ime_test tests/ime_test.cpp src/ime.cpp ${IMSHELL_TEST_IMGUI})
    target_include_directories(imshell_ime_test PRIVATE src ${IMSHELL_IMGUI_DIR})
    target_compile_features(imshell_ime_test PRIVATE cxx_std_20)
    add_test(NAME imshell_ime_test COMMAND imshell_ime_test)
    add_executable(imshell_handles_test tests/handles_test.cpp src/touch.cpp ${IMSHELL_TEST_IMGUI})
    target_include_directories(imshell_handles_test PRIVATE src ${IMSHELL_IMGUI_DIR})
    target_compile_features(imshell_handles_test PRIVATE cxx_std_20)
    add_test(NAME imshell_handles_test COMMAND imshell_handles_test)
    add_executable(imshell_components_test tests/components_test.cpp src/components.cpp src/button.cpp ${IMSHELL_TEST_IMGUI})
    target_include_directories(imshell_components_test PRIVATE include ${IMSHELL_IMGUI_DIR})
    target_compile_features(imshell_components_test PRIVATE cxx_std_20)
    target_compile_definitions(imshell_components_test PRIVATE IMGUI_ENABLE_TEST_ENGINE)
    add_test(NAME imshell_components_test COMMAND imshell_components_test)
    # the emoji loader this platform builds (see above)
    get_target_property(IMSHELL_SOURCES imshell SOURCES)
    set(IMSHELL_EMOJI_SOURCES src/emoji_glyph.cpp)
    foreach(f src/apple/emoji.mm src/windows/emoji.cpp src/emoji_freetype.cpp src/emoji_none.cpp)
        if(f IN_LIST IMSHELL_SOURCES)
            list(APPEND IMSHELL_EMOJI_SOURCES ${f})
        endif()
    endforeach()
    add_executable(imshell_emoji_test tests/emoji_test.cpp ${IMSHELL_EMOJI_SOURCES} ${IMSHELL_TEST_IMGUI})
    target_include_directories(imshell_emoji_test PRIVATE include src ${IMSHELL_IMGUI_DIR})
    target_compile_features(imshell_emoji_test PRIVATE cxx_std_20)
    if(APPLE)
        target_link_libraries(imshell_emoji_test PRIVATE "-framework CoreText" "-framework CoreFoundation" "-framework CoreGraphics")
    elseif(WIN32)
        target_link_libraries(imshell_emoji_test PRIVATE dwrite d2d1)
    elseif(FREETYPE_FOUND)
        target_link_libraries(imshell_emoji_test PRIVATE Freetype::Freetype)
    endif()
    add_test(NAME imshell_emoji_test COMMAND imshell_emoji_test)
    add_executable(imshell_resizable_test tests/resizable_test.cpp src/resizable.cpp ${IMSHELL_TEST_IMGUI})
    target_include_directories(imshell_resizable_test PRIVATE include ${IMSHELL_IMGUI_DIR})
    target_compile_features(imshell_resizable_test PRIVATE cxx_std_20)
    target_compile_definitions(imshell_resizable_test PRIVATE IMGUI_ENABLE_TEST_ENGINE)
    add_test(NAME imshell_resizable_test COMMAND imshell_resizable_test)
    add_executable(imshell_editor_test tests/editor_test.cpp src/editor.cpp src/button.cpp ${IMSHELL_TEST_IMGUI})
    target_include_directories(imshell_editor_test PRIVATE include ${IMSHELL_IMGUI_DIR})
    target_compile_features(imshell_editor_test PRIVATE cxx_std_20)
    target_compile_definitions(imshell_editor_test PRIVATE IMGUI_ENABLE_TEST_ENGINE)
    add_test(NAME imshell_editor_test COMMAND imshell_editor_test)
    if(TARGET imshell_node_editor)
        add_executable(imshell_node_editor_test tests/node_editor_test.cpp src/node_editor.cpp
            ${IMSHELL_NODE_EDITOR_DIR}/imgui_node_editor.cpp ${IMSHELL_NODE_EDITOR_DIR}/imgui_node_editor_api.cpp
            ${IMSHELL_NODE_EDITOR_DIR}/crude_json.cpp ${IMSHELL_NODE_EDITOR_DIR}/imgui_canvas.cpp ${IMSHELL_TEST_IMGUI})
        target_include_directories(imshell_node_editor_test PRIVATE include ${IMSHELL_IMGUI_DIR} ${IMSHELL_NODE_EDITOR_DIR})
        target_compile_features(imshell_node_editor_test PRIVATE cxx_std_20)
        add_test(NAME imshell_node_editor_test COMMAND imshell_node_editor_test)
    endif()
    if(TARGET imgui_rich_md)
        # fonts.cpp and the emoji loader: markdown merges emoji into its faces
        add_executable(imshell_markdown_test tests/markdown_test.cpp src/markdown.cpp src/editor.cpp src/button.cpp
                       src/fonts.cpp ${IMSHELL_EMOJI_SOURCES} ${IMSHELL_TEST_IMGUI})
        target_include_directories(imshell_markdown_test PRIVATE include src third_party ${IMSHELL_IMGUI_DIR})
        target_compile_features(imshell_markdown_test PRIVATE cxx_std_20)
        target_link_libraries(imshell_markdown_test PRIVATE imgui_rich_md)
        if(APPLE)
            target_link_libraries(imshell_markdown_test PRIVATE "-framework CoreText" "-framework CoreFoundation" "-framework CoreGraphics")
        elseif(WIN32)
            target_link_libraries(imshell_markdown_test PRIVATE dwrite d2d1)
        elseif(FREETYPE_FOUND)
            target_link_libraries(imshell_markdown_test PRIVATE Freetype::Freetype)
        endif()
        add_test(NAME imshell_markdown_test COMMAND imshell_markdown_test)
    endif()
    # the smoke test: hello drives its own window (title bar, native views, the close veto) and
    # prints "probe: ok"; needs a desktop session
    if(TARGET imshell_hello AND NOT ANDROID AND NOT EMSCRIPTEN)
        add_test(NAME imshell_hello_smoke COMMAND imshell_hello)
        set_tests_properties(imshell_hello_smoke PROPERTIES
            ENVIRONMENT "IMSHELL_HELLO_PROBE=1;IMSHELL_HELLO_QUIT=20"
            PASS_REGULAR_EXPRESSION "probe: ok" FAIL_REGULAR_EXPRESSION "probe: FAILED")
    endif()
endif()

# an app as people get it: imshell_app(<target> [NAME <name>] [ID <bundle id>] [VERSION <v>]
# [APPCAST <url>] [PUBLIC_KEY <ed25519 key>]). windows, with the updater: WinSparkle.dll next to
# the exe. macos: a <target>-app target that makes <NAME>.app and <target>-macos-<arch>.zip in the
# build folder (cmake/macos-app.cmake: bundle, sparkle, signature, notarizing)
function(imshell_app target)
    cmake_parse_arguments(A "" "NAME;ID;VERSION;APPCAST;PUBLIC_KEY" "" ${ARGN})
    if(IOS OR ANDROID OR EMSCRIPTEN)
        return()
    endif()
    if(WIN32 AND IMSHELL_UPDATER)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:WinSparkle> $<TARGET_FILE_DIR:${target}>)
    elseif(APPLE)
        set(sparkle "")
        if(IMSHELL_UPDATER)
            set(sparkle "${IMSHELL_SPARKLE_FRAMEWORK}")
        endif()
        set(arch "${CMAKE_OSX_ARCHITECTURES}")
        if(NOT arch)
            set(arch "${CMAKE_HOST_SYSTEM_PROCESSOR}")
        elseif(arch MATCHES ";")
            set(arch universal)
        endif()
        if(NOT A_NAME)
            set(A_NAME ${target})
        endif()
        if(NOT A_VERSION)
            set(A_VERSION "${PROJECT_VERSION}")
        endif()
        add_custom_target(${target}-app
            COMMAND ${CMAKE_COMMAND} "-DEXE=$<TARGET_FILE:${target}>" "-DNAME=${A_NAME}" "-DID=${A_ID}"
                    "-DVERSION=${A_VERSION}" "-DMIN_OS=${CMAKE_OSX_DEPLOYMENT_TARGET}" "-DAPPCAST=${A_APPCAST}"
                    "-DPUBLIC_KEY=${A_PUBLIC_KEY}" "-DSPARKLE=${sparkle}" "-DOUT=${CMAKE_BINARY_DIR}"
                    "-DZIP=${CMAKE_BINARY_DIR}/${target}-macos-${arch}.zip"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cmake/macos-app.cmake"
            DEPENDS ${target} VERBATIM USES_TERMINAL)
    endif()
endfunction()
