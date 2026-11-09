cmake_minimum_required(VERSION 3.23)
project(mydear LANGUAGES CXX)

# the host compiles Dear ImGui once and hands over its folder: a second copy of imgui's
# statics would silently split the context
set(MYDEAR_IMGUI_DIR "" CACHE PATH "Dear ImGui source folder (imgui.h, imconfig.h)")
if(NOT MYDEAR_IMGUI_DIR)
    message(FATAL_ERROR "mydear: set MYDEAR_IMGUI_DIR to the host's Dear ImGui folder")
endif()

add_library(mydear STATIC
    src/button.cpp
    src/embedded.cpp
    src/image.cpp
    src/select.cpp
    src/spinner.cpp
    src/toggle_button.cpp
)
add_library(mydear::mydear ALIAS mydear)
target_compile_features(mydear PUBLIC cxx_std_20)
target_include_directories(mydear PUBLIC include ${MYDEAR_IMGUI_DIR} PRIVATE third_party)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/MydearEmbed.cmake)
