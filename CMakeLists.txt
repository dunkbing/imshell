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
    src/button.cpp
    src/embedded.cpp
    src/image.cpp
    src/select.cpp
    src/spinner.cpp
    src/toggle_button.cpp
)
add_library(mydear::mydear ALIAS mydear)

# the platform shell. imgui core stays the host's; its platform backends are ours
if(IOS)
    target_sources(mydear PRIVATE src/ios/app.mm ${MYDEAR_IMGUI_DIR}/backends/imgui_impl_metal.mm)
    set_source_files_properties(src/ios/app.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(mydear PUBLIC "-framework UIKit" "-framework Metal" "-framework MetalKit"
        "-framework QuartzCore" "-framework Foundation")
elseif(APPLE)
    find_package(glfw3 CONFIG REQUIRED)
    target_sources(mydear PRIVATE
        src/macos/app.mm
        src/macos/metal_backend.mm
        ${MYDEAR_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${MYDEAR_IMGUI_DIR}/backends/imgui_impl_metal.mm
    )
    target_link_libraries(mydear PUBLIC glfw
        "-framework Metal" "-framework QuartzCore" "-framework AppKit" "-framework Foundation"
        "-framework ImageIO" "-framework CoreGraphics" "-framework UniformTypeIdentifiers")
endif()
target_compile_features(mydear PUBLIC cxx_std_20)
target_include_directories(mydear PUBLIC include ${MYDEAR_IMGUI_DIR} ${MYDEAR_IMGUI_DIR}/backends
                                  PRIVATE third_party)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MydearEmbed.cmake)

# examples are hosts too: each compiles its own imgui core
option(MYDEAR_BUILD_EXAMPLES "Build the examples" OFF)
if(MYDEAR_BUILD_EXAMPLES AND APPLE AND NOT IOS)
    file(GLOB MYDEAR_IMGUI_CORE ${MYDEAR_IMGUI_DIR}/imgui*.cpp)
    add_executable(mydear_hello examples/hello/main.cpp ${MYDEAR_IMGUI_CORE})
    target_link_libraries(mydear_hello PRIVATE mydear)
endif()
