cmake_minimum_required(VERSION 3.23)
project(mydear LANGUAGES CXX)

# the host compiles Dear ImGui once and hands over its folder: a second copy of imgui's
# statics would silently split the context
set(MYDEAR_IMGUI_DIR "" CACHE PATH "Dear ImGui source folder (imgui.h, imconfig.h)")
if(NOT MYDEAR_IMGUI_DIR)
    message(FATAL_ERROR "mydear: set MYDEAR_IMGUI_DIR to the host's Dear ImGui folder")
endif()

add_library(mydear STATIC
    src/activity.cpp
    src/alert.cpp
    src/app.cpp
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
add_library(mydear::mydear ALIAS mydear)

# optional components (mydear/components.hpp): a host links mydear::components to use them
add_library(mydear_components STATIC src/components.cpp)
add_library(mydear::components ALIAS mydear_components)
target_link_libraries(mydear_components PUBLIC mydear)

# the platform shell. imgui core stays the host's; its platform backends are ours
if(EMSCRIPTEN) # before UNIX, which emscripten's toolchain also sets
    target_sources(mydear PRIVATE
        src/web/app.cpp
        src/haptics_none.cpp
        src/native_view.cpp
        ${MYDEAR_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${MYDEAR_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    # glfw on a canvas (emscripten's contrib port: hidpi, resizing, clipboard), webgl2
    target_compile_options(mydear PUBLIC --use-port=contrib.glfw3)
    target_link_options(mydear PUBLIC --use-port=contrib.glfw3 -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2
                                      -sALLOW_MEMORY_GROWTH=1)
elseif(IOS)
    target_sources(mydear PRIVATE src/ios/app.mm src/ios/file_dialog.mm src/apple/haptics.mm src/apple/native_view.mm src/native_view.cpp src/touch.cpp
                                  ${MYDEAR_IMGUI_DIR}/backends/imgui_impl_metal.mm)
    set_source_files_properties(src/ios/app.mm src/ios/file_dialog.mm src/apple/haptics.mm src/apple/native_view.mm
                                PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(mydear PUBLIC "-framework UIKit" "-framework Metal" "-framework MetalKit"
        "-framework QuartzCore" "-framework Foundation" "-framework UniformTypeIdentifiers")
elseif(APPLE)
    find_package(glfw3 CONFIG REQUIRED)
    target_sources(mydear PRIVATE
        src/macos/app.mm
        src/macos/metal_backend.mm
        src/macos/native_button.mm
        src/apple/haptics.mm
        src/apple/native_view.mm
        src/native_view.cpp
        ${MYDEAR_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${MYDEAR_IMGUI_DIR}/backends/imgui_impl_metal.mm
    )
    # arc for both: they share MetalBackend, whose id members must be owned one way
    set_source_files_properties(src/macos/app.mm src/macos/metal_backend.mm src/macos/native_button.mm
                                src/apple/haptics.mm src/apple/native_view.mm
                                PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(mydear PUBLIC glfw
        "-framework Metal" "-framework QuartzCore" "-framework AppKit" "-framework Foundation"
        "-framework ImageIO" "-framework CoreGraphics" "-framework UniformTypeIdentifiers")
elseif(WIN32)
    find_package(glfw3 CONFIG REQUIRED)
    target_sources(mydear PRIVATE
        src/windows/app.cpp
        src/windows/dx11_backend.cpp
        src/windows/native_view.cpp
        src/haptics_none.cpp
        src/native_view.cpp
        ${MYDEAR_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${MYDEAR_IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_compile_definitions(mydear PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(mydear PUBLIC glfw d3d11 dxgi dwmapi)
elseif(ANDROID)
    # NativeActivity through the ndk's glue; its entry point must survive static linking
    enable_language(C)
    set(MYDEAR_GLUE_DIR ${ANDROID_NDK}/sources/android/native_app_glue)
    target_sources(mydear PRIVATE
        src/android/app.cpp
        src/android/file_dialog.cpp
        src/touch.cpp
        src/native_view.cpp
        ${MYDEAR_GLUE_DIR}/android_native_app_glue.c
        ${MYDEAR_IMGUI_DIR}/backends/imgui_impl_android.cpp
        ${MYDEAR_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(mydear PUBLIC ${MYDEAR_GLUE_DIR})
    target_link_libraries(mydear PUBLIC android EGL GLESv3 log)
    target_link_options(mydear INTERFACE -u ANativeActivity_onCreate)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MydearAndroid.cmake)
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MYDEAR_GTK4 REQUIRED IMPORTED_TARGET gtk4)
    pkg_check_modules(MYDEAR_EPOXY REQUIRED IMPORTED_TARGET epoxy)
    target_sources(mydear PRIVATE
        src/linux/app.cpp
        src/haptics_none.cpp
        src/native_view.cpp
        ${MYDEAR_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_link_libraries(mydear PUBLIC PkgConfig::MYDEAR_GTK4 PkgConfig::MYDEAR_EPOXY)
endif()

# desktop file dialogs: native file dialog extended (vcpkg nativefiledialog-extended)
if(NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(nfd CONFIG REQUIRED)
    target_sources(mydear PRIVATE src/file_dialog_nfd.cpp)
    target_link_libraries(mydear PUBLIC nfd::nfd)
endif()

# auto-update (mydear/updater.hpp). the host packages the updater: on macos it sets
# MYDEAR_SPARKLE_FRAMEWORK and embeds the framework, on windows it defines a WinSparkle target
# and ships the dll; on linux mydear finds httplib, nlohmann_json and openssl
option(MYDEAR_UPDATER "auto-update through sparkle, winsparkle or an appimage swap" OFF)
set(MYDEAR_SPARKLE_FRAMEWORK "" CACHE PATH "macos: Sparkle.framework")
if(NOT MYDEAR_UPDATER OR IOS OR ANDROID OR EMSCRIPTEN)
    target_sources(mydear PRIVATE src/updater_none.cpp)
elseif(APPLE)
    target_sources(mydear PRIVATE src/macos/updater.mm)
    set_source_files_properties(src/macos/updater.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_include_directories(mydear PRIVATE "${MYDEAR_SPARKLE_FRAMEWORK}/Headers")
    target_link_libraries(mydear PUBLIC "${MYDEAR_SPARKLE_FRAMEWORK}")
elseif(WIN32)
    target_sources(mydear PRIVATE src/windows/updater.cpp)
    target_link_libraries(mydear PUBLIC WinSparkle)
else()
    find_package(httplib CONFIG REQUIRED)
    find_package(nlohmann_json CONFIG REQUIRED)
    find_package(OpenSSL REQUIRED)
    target_sources(mydear PRIVATE src/linux/updater.cpp)
    target_link_libraries(mydear PRIVATE httplib::httplib nlohmann_json::nlohmann_json
                                         OpenSSL::SSL OpenSSL::Crypto)
endif()

target_compile_features(mydear PUBLIC cxx_std_20)
target_include_directories(mydear PUBLIC include ${MYDEAR_IMGUI_DIR} ${MYDEAR_IMGUI_DIR}/backends
                                  PRIVATE third_party)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MydearEmbed.cmake)

# examples are hosts too: each compiles its own imgui core
option(MYDEAR_BUILD_EXAMPLES "Build the examples" OFF)
if(MYDEAR_BUILD_EXAMPLES AND NOT IOS)
    file(GLOB MYDEAR_IMGUI_CORE ${MYDEAR_IMGUI_DIR}/imgui*.cpp)
    if(ANDROID)
        add_library(mydear_hello SHARED examples/hello/main.cpp examples/hello/native_android.cpp ${MYDEAR_IMGUI_CORE})
        mydear_android_apk(mydear_hello PACKAGE io.mydear.hello LABEL "Hello mydear")
    elseif(EMSCRIPTEN) # hello.html (+ .js, .wasm); serve the folder
        add_executable(mydear_hello examples/hello/main.cpp examples/hello/native_web.cpp ${MYDEAR_IMGUI_CORE})
        set_target_properties(mydear_hello PROPERTIES SUFFIX .html)
        target_link_options(mydear_hello PRIVATE --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/examples/web_shell.html)
    elseif(APPLE)
        add_executable(mydear_hello examples/hello/main.cpp examples/hello/native_macos.mm ${MYDEAR_IMGUI_CORE})
        set_source_files_properties(examples/hello/native_macos.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    elseif(WIN32)
        add_executable(mydear_hello examples/hello/main.cpp examples/hello/native_windows.cpp ${MYDEAR_IMGUI_CORE})
    else()
        add_executable(mydear_hello examples/hello/main.cpp examples/hello/native_linux.cpp ${MYDEAR_IMGUI_CORE})
    endif()
    target_link_libraries(mydear_hello PRIVATE mydear)
    # the components gallery; on the web it is the site's demo
    if(NOT ANDROID)
        add_executable(mydear_gallery examples/gallery/main.cpp ${MYDEAR_IMGUI_CORE})
        target_link_libraries(mydear_gallery PRIVATE mydear_components)
        if(EMSCRIPTEN)
            set_target_properties(mydear_gallery PROPERTIES SUFFIX .html)
            target_link_options(mydear_gallery PRIVATE --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/examples/web_shell.html)
        endif()
    endif()
endif()

# the touch engine's test: a bare imgui context, no platform
option(MYDEAR_BUILD_TESTS "Build the tests" OFF)
if(MYDEAR_BUILD_TESTS)
    enable_testing()
    file(GLOB MYDEAR_TEST_IMGUI ${MYDEAR_IMGUI_DIR}/imgui*.cpp)
    add_executable(mydear_touch_test tests/touch_test.cpp src/touch.cpp ${MYDEAR_TEST_IMGUI})
    target_include_directories(mydear_touch_test PRIVATE src ${MYDEAR_IMGUI_DIR})
    target_compile_features(mydear_touch_test PRIVATE cxx_std_20)
    add_test(NAME mydear_touch_test COMMAND mydear_touch_test)
    add_executable(mydear_frame_skip_test tests/frame_skip_test.cpp src/frame_skip.cpp ${MYDEAR_TEST_IMGUI})
    target_include_directories(mydear_frame_skip_test PRIVATE src ${MYDEAR_IMGUI_DIR})
    target_compile_features(mydear_frame_skip_test PRIVATE cxx_std_20)
    add_test(NAME mydear_frame_skip_test COMMAND mydear_frame_skip_test)
    add_executable(mydear_ime_test tests/ime_test.cpp src/ime.cpp ${MYDEAR_TEST_IMGUI})
    target_include_directories(mydear_ime_test PRIVATE src ${MYDEAR_IMGUI_DIR})
    target_compile_features(mydear_ime_test PRIVATE cxx_std_20)
    add_test(NAME mydear_ime_test COMMAND mydear_ime_test)
    add_executable(mydear_components_test tests/components_test.cpp src/components.cpp src/button.cpp ${MYDEAR_TEST_IMGUI})
    target_include_directories(mydear_components_test PRIVATE include ${MYDEAR_IMGUI_DIR})
    target_compile_features(mydear_components_test PRIVATE cxx_std_20)
    target_compile_definitions(mydear_components_test PRIVATE IMGUI_ENABLE_TEST_ENGINE)
    add_test(NAME mydear_components_test COMMAND mydear_components_test)
    # the smoke test: hello drives its own window (title bar, native views, the close veto) and
    # prints "probe: ok"; needs a desktop session
    if(TARGET mydear_hello AND NOT ANDROID AND NOT EMSCRIPTEN)
        add_test(NAME mydear_hello_smoke COMMAND mydear_hello)
        set_tests_properties(mydear_hello_smoke PROPERTIES
            ENVIRONMENT "MYDEAR_HELLO_PROBE=1;MYDEAR_HELLO_QUIT=20"
            PASS_REGULAR_EXPRESSION "probe: ok" FAIL_REGULAR_EXPRESSION "probe: FAILED")
    endif()
endif()
