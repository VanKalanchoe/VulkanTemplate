cmake_minimum_required(VERSION 3.16)

project(imgui LANGUAGES CXX)

add_library(imgui
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    imgui_demo.cpp
    backends/imgui_impl_sdl3.cpp
    backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/backends
)

target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)

set_target_properties(imgui PROPERTIES CXX_STANDARD 20)
