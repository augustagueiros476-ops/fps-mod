cmake_minimum_required(VERSION 3.18)
project(mod_menu CXX C)
set(CMAKE_CXX_STANDARD 17)

set(DOBBY_GENERATE_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/Dobby)

set(IMGUI ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(mod_menu SHARED
    src/main.cpp
    ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_opengl3.cpp)
target_include_directories(mod_menu PRIVATE ${IMGUI} ${IMGUI}/backends third_party/Dobby/include)
target_link_libraries(mod_menu PRIVATE dobby log android EGL GLESv3)
