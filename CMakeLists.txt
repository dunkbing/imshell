cmake_minimum_required(VERSION 3.23)
project(imshell LANGUAGES CXX)

# the host compiles Dear ImGui once and hands over its folder: a second copy of imgui's
# statics would silently split the context
set(IMSHELL_IMGUI_DIR "" CACHE PATH "Dear ImGui source folder (imgui.h, imconfig.h)")
if(NOT IMSHELL_IMGUI_DIR)
    message(FATAL_ERROR "imshell: set IMSHELL_IMGUI_DIR to the host's Dear ImGui folder")
endif()

add_library(imshell STATIC
    src/activity.cpp
    src/alert.cpp
    src/app.cpp
    src/data_dir.cpp
    src/button.cpp
    src/embedded.cpp
    src/fonts.cpp
    src/frame_skip.cpp
    src/ime.cpp
    src/image.cpp
    src/select.cpp
    src/spinner.cpp
    src/toggle_button.cpp
)
add_library(imshell::imshell ALIAS imshell)

# optional components (imshell/components.hpp): a host links imshell::components to use them
add_library(imshell_components STATIC src/components.cpp)
add_library(imshell::components ALIAS imshell_components)
target_link_libraries(imshell_components PUBLIC imshell)

# optional code editor (imshell/editor.hpp): a host links imshell::editor to use it
add_library(imshell_editor STATIC src/editor.cpp)
add_library(imshell::editor ALIAS imshell_editor)
target_link_libraries(imshell_editor PUBLIC imshell)

# optional markdown (imshell/markdown.hpp) through imgui_rich_md, which the host provides like
# imgui: its folder in IMSHELL_RICH_MD_DIR, or its own imgui_rich_md target added before imshell.
# without either there is no imshell::markdown
set(IMSHELL_RICH_MD_DIR "" CACHE PATH "imgui_rich_md source folder, for imshell::markdown")
if(IMSHELL_RICH_MD_DIR AND NOT TARGET imgui_rich_md)
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
endif()

# the platform shell. imgui core stays the host's; its platform backends are ours
if(EMSCRIPTEN) # before UNIX, which emscripten's toolchain also sets
    target_sources(imshell PRIVATE
        src/web/app.cpp
        src/haptics_none.cpp
        src/native_view.cpp
        ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    # glfw on a canvas (emscripten's contrib port: hidpi, resizing, clipboard), webgl2
    target_compile_options(imshell PUBLIC --use-port=contrib.glfw3)
    target_link_options(imshell PUBLIC --use-port=contrib.glfw3 -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2
                                      -sALLOW_MEMORY_GROWTH=1)
elseif(IOS)
    target_sources(imshell PRIVATE src/ios/app.mm src/ios/file_dialog.mm src/apple/haptics.mm src/apple/native_view.mm src/native_view.cpp src/touch.cpp
                                  ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_metal.mm)
    set_source_files_properties(src/ios/app.mm src/ios/file_dialog.mm src/apple/haptics.mm src/apple/native_view.mm
                                PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(imshell PUBLIC "-framework UIKit" "-framework Metal" "-framework MetalKit"
        "-framework QuartzCore" "-framework Foundation" "-framework UniformTypeIdentifiers")
elseif(APPLE)
    find_package(glfw3 CONFIG REQUIRED)
    target_sources(imshell PRIVATE
        src/macos/app.mm
        src/macos/metal_backend.mm
        src/macos/native_button.mm
        src/apple/haptics.mm
        src/apple/native_view.mm
        src/native_view.cpp
        ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_metal.mm
    )
    # arc for both: they share MetalBackend, whose id members must be owned one way
    set_source_files_properties(src/macos/app.mm src/macos/metal_backend.mm src/macos/native_button.mm
                                src/apple/haptics.mm src/apple/native_view.mm
                                PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(imshell PUBLIC glfw
        "-framework Metal" "-framework QuartzCore" "-framework AppKit" "-framework Foundation"
        "-framework ImageIO" "-framework CoreGraphics" "-framework UniformTypeIdentifiers")
elseif(WIN32)
    find_package(glfw3 CONFIG REQUIRED)
    target_sources(imshell PRIVATE
        src/windows/app.cpp
        src/windows/dx11_backend.cpp
        src/windows/native_view.cpp
        src/haptics_none.cpp
        src/native_view.cpp
        ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMSHELL_IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_compile_definitions(imshell PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(imshell PUBLIC glfw d3d11 dxgi dwmapi)
elseif(ANDROID)
    # NativeActivity through the ndk's glue; its entry point must survive static linking
    enable_language(C)
    set(IMSHELL_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
    target_sources(imshell PRIVATE
        src/android/app.cpp
        src/android/file_dialog.cpp
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

# auto-update (imshell/updater.hpp). the host packages the updater: on macos it sets
# IMSHELL_SPARKLE_FRAMEWORK and embeds the framework, on windows it defines a WinSparkle target
# and ships the dll; on linux imshell finds httplib, nlohmann_json and openssl
option(IMSHELL_UPDATER "auto-update through sparkle, winsparkle or an appimage swap" OFF)
set(IMSHELL_SPARKLE_FRAMEWORK "" CACHE PATH "macos: Sparkle.framework")
if(NOT IMSHELL_UPDATER OR IOS OR ANDROID OR EMSCRIPTEN)
    target_sources(imshell PRIVATE src/updater_none.cpp)
elseif(APPLE)
    target_sources(imshell PRIVATE src/macos/updater.mm)
    set_source_files_properties(src/macos/updater.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_include_directories(imshell PRIVATE "${IMSHELL_SPARKLE_FRAMEWORK}/Headers")
    target_link_libraries(imshell PUBLIC "${IMSHELL_SPARKLE_FRAMEWORK}")
elseif(WIN32)
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
        imshell_android_apk(imshell_hello PACKAGE io.imshell.hello LABEL "Hello imshell")
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

# the touch engine's test: a bare imgui context, no platform
option(IMSHELL_BUILD_TESTS "Build the tests" OFF)
if(IMSHELL_BUILD_TESTS)
    enable_testing()
    file(GLOB IMSHELL_TEST_IMGUI ${IMSHELL_IMGUI_DIR}/imgui*.cpp)
    add_executable(imshell_touch_test tests/touch_test.cpp src/touch.cpp ${IMSHELL_TEST_IMGUI})
    target_include_directories(imshell_touch_test PRIVATE src ${IMSHELL_IMGUI_DIR})
    target_compile_features(imshell_touch_test PRIVATE cxx_std_20)
    add_test(NAME imshell_touch_test COMMAND imshell_touch_test)
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
    add_executable(imshell_editor_test tests/editor_test.cpp src/editor.cpp src/button.cpp ${IMSHELL_TEST_IMGUI})
    target_include_directories(imshell_editor_test PRIVATE include ${IMSHELL_IMGUI_DIR})
    target_compile_features(imshell_editor_test PRIVATE cxx_std_20)
    target_compile_definitions(imshell_editor_test PRIVATE IMGUI_ENABLE_TEST_ENGINE)
    add_test(NAME imshell_editor_test COMMAND imshell_editor_test)
    if(TARGET imgui_rich_md)
        add_executable(imshell_markdown_test tests/markdown_test.cpp src/markdown.cpp src/editor.cpp src/button.cpp ${IMSHELL_TEST_IMGUI})
        target_include_directories(imshell_markdown_test PRIVATE include third_party ${IMSHELL_IMGUI_DIR})
        target_compile_features(imshell_markdown_test PRIVATE cxx_std_20)
        target_link_libraries(imshell_markdown_test PRIVATE imgui_rich_md)
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
