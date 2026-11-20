# a render-only video project: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DIMSHELL_DIR=/path/to/imshell && cmake --build build
# glfw is fetched when it is not installed, and no file dialog library is needed; Linux renders through ffmpeg, so it needs ffmpeg on PATH
cmake_minimum_required(VERSION 3.23)
project(starter LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(IMSHELL_DIR "" CACHE PATH "an imshell checkout with its submodules")
set(IMSHELL_FILE_DIALOGS OFF CACHE BOOL "" FORCE)
add_subdirectory(${IMSHELL_DIR} imshell)
file(GLOB IMGUI_CORE ${IMSHELL_IMGUI_DIR}/imgui*.cpp)
add_executable(starter main.cpp ${IMGUI_CORE})
target_link_libraries(starter PRIVATE imshell::video)
