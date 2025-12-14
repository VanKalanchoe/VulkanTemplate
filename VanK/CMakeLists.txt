add_library(VanK STATIC)

# Match VS filters to directory structure on disk
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/CMake")

# Add option to enable/disable C++ 20 module
option(ENABLE_CPP20_MODULE "Enable C++ 20 module support for Vulkan" OFF)

# Enable C++ module dependency scanning only if C++ 20 module is enabled
if(ENABLE_CPP20_MODULE)
  set(CMAKE_CXX_SCAN_FOR_MODULES ON)
endif()

find_package (sdl3 REQUIRED)
find_package (glm REQUIRED)
find_package (Vulkan REQUIRED)
find_package (tinyobjloader REQUIRED)
find_package (tinygltf REQUIRED)
find_package (KTX REQUIRED)
find_package (xxhash REQUIRED)
find_package (spdlog CONFIG REQUIRED)
find_package (yaml-cpp CONFIG REQUIRED)
find_package (box2d CONFIG REQUIRED)

include(FetchContent)

FetchContent_Declare(
    ImGuizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        master   # or a specific tag, e.g. 1.89
)

FetchContent_MakeAvailable(ImGuizmo)

# ImGuizmo is a header-only + one source file library
add_library(ImGuizmoLib STATIC
    ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp
)

target_include_directories(ImGuizmoLib PUBLIC
    ${imguizmo_SOURCE_DIR}
)

target_link_libraries(ImGuizmoLib PUBLIC imgui)

set(IMGUI_SOURCES
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/backends/imgui_impl_sdl3.cpp
    vendor/imgui/backends/imgui_impl_vulkan.cpp
    vendor/imgui/misc/cpp/imgui_stdlib.cpp
)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC vendor/imgui)
target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Vulkan)

# set up Vulkan C++ module only if enabled
if(ENABLE_CPP20_MODULE)
    add_library(VulkanCppModule)
    add_library(Vulkan::cppm ALIAS VulkanCppModule)

    target_compile_definitions(VulkanCppModule
        PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
    )

    target_include_directories(VulkanCppModule
        PUBLIC
        "${Vulkan_INCLUDE_DIR}"
    )
    target_link_libraries(VulkanCppModule
        PUBLIC
        Vulkan::Vulkan
    )

    set_target_properties(VulkanCppModule PROPERTIES CXX_STANDARD 23)

    # Add MSVC-specific compiler options for proper C++ module support
    if(MSVC)
        target_compile_options(VulkanCppModule PRIVATE
            /std:c++latest      # Use latest C++ standard for better module support
            /permissive-        # Standards conformance mode
            /Zc:__cplusplus     # Enable correct __cplusplus macro
            /EHsc               # Enable C++ exception handling
            /Zc:preprocessor    # Use conforming preprocessor
            /translateInclude   # Automatically translate #include to import for standard library
            )
    endif()

    target_sources(VulkanCppModule
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS
        "${Vulkan_INCLUDE_DIR}"
        FILES
        "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
    )

    # Add the vulkan.cppm file directly as a source file
    target_sources(VulkanCppModule
        PRIVATE
        "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
    )
else()
  # Create a dummy interface library when C++ 20 module is disabled
  add_library(VulkanCppModule INTERFACE)
  add_library(Vulkan::cppm ALIAS VulkanCppModule)
  target_link_libraries(VulkanCppModule INTERFACE Vulkan::Vulkan)
  target_compile_definitions(VulkanCppModule
          INTERFACE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
  )
endif()

find_package(stb REQUIRED)
set(STB_INCLUDEDIR ${stb_INCLUDE_DIRS})

function(add_chapter CHAPTER_NAME)
    cmake_parse_arguments(CHAPTER "" "" "SHADER;LIBS;TEXTURES;MODELS" ${ARGN})

    # Gather all .cpp files in the chapter folder
    file(GLOB_RECURSE CHAPTER_SOURCES
    "${CMAKE_SOURCE_DIR}/${CHAPTER_NAME}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/${CHAPTER_NAME}/src/*.h"
    "${CMAKE_SOURCE_DIR}/${CHAPTER_NAME}/src/*.hpp"
    "${CMAKE_SOURCE_DIR}/VanK-Editor/shaders/shaderIO.h"
    )

    target_include_directories(${CHAPTER_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/VanK/src ${CMAKE_CURRENT_SOURCE_DIR}/Vendor/slang/include ${CMAKE_CURRENT_SOURCE_DIR}/Vendor/FileWatch ${CMAKE_CURRENT_SOURCE_DIR}/Vendor/Entt/include ${CMAKE_CURRENT_SOURCE_DIR}/Vendor/VulkanMemoryAllocator-Hpp/VulkanMemoryAllocator/include ${CMAKE_CURRENT_SOURCE_DIR}/Vendor/VulkanMemoryAllocator-Hpp/include "${CMAKE_SOURCE_DIR}/VanK-Editor/shaders")
message("VMA include dir: ${CMAKE_CURRENT_SOURCE_DIR}/VanK/vendor/VulkanMemoryAllocator-Hpp/include")

    set_target_properties(${CHAPTER_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CHAPTER_NAME}
        CXX_STANDARD 23
    )

    # Link Vulkan, SDL, and any additional libraries
    target_link_libraries(${CHAPTER_NAME} Vulkan::cppm SDL3::SDL3 "${CMAKE_CURRENT_SOURCE_DIR}/Vendor/slang/lib/slang.lib")

    # Set working directory for Visual Studio debug
    if(WIN32 AND ${CMAKE_GENERATOR} MATCHES "Visual Studio.*")
        set_target_properties(${CHAPTER_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/${CHAPTER_NAME}")
    endif()

    target_compile_definitions(${CHAPTER_NAME} PRIVATE
        $<$<CONFIG:Debug>:VK_DEBUG>
        $<$<CONFIG:Release>:VK_RELEASE>
        $<$<CONFIG:RelWithDebInfo>:VK_RELWITHDEBINFO>
    )

    # Link additional libraries if defined
    if(DEFINED CHAPTER_LIBS)
        target_link_libraries(${CHAPTER_NAME} ${CHAPTER_LIBS})
    endif()

    # Copy models to build folder
    if(DEFINED CHAPTER_MODELS)
        list(TRANSFORM CHAPTER_MODELS PREPEND "${CMAKE_SOURCE_DIR}/assets/")
        file(COPY ${CHAPTER_MODELS} DESTINATION ${CMAKE_BINARY_DIR}/${CHAPTER_NAME}/models)
    endif()

    # Copy textures to build folder
    if(DEFINED CHAPTER_TEXTURES)
        list(TRANSFORM CHAPTER_TEXTURES PREPEND "${CMAKE_SOURCE_DIR}/assets/")
        file(COPY ${CHAPTER_TEXTURES} DESTINATION ${CMAKE_BINARY_DIR}/${CHAPTER_NAME}/textures)
    endif()

    # Copy shaders to build folder
    if(DEFINED CHAPTER_SHADER)
        list(TRANSFORM CHAPTER_SHADER PREPEND "${CMAKE_SOURCE_DIR}/VanK-Editor/shaders/")
        file(COPY ${CHAPTER_SHADER} DESTINATION ${CMAKE_BINARY_DIR}/${CHAPTER_NAME}/shaders)
    endif()

    add_custom_command(TARGET ${CHAPTER_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/VanK/Vendor/slang/bin/slang.dll"
            $<TARGET_FILE_DIR:${CHAPTER_NAME}>
    )

    add_custom_command(TARGET ${CHAPTER_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/VanK/Vendor/slang/bin/slang-glslang.dll"
            $<TARGET_FILE_DIR:${CHAPTER_NAME}>
    )

    # Attach sources to the target
target_sources(${CHAPTER_NAME} PRIVATE ${CHAPTER_SOURCES})
endfunction()

add_chapter(
    VanK
    SHADER shaderIO.h shader.slang DrawIndirectShader.slang
    MODELS viking_room.obj viking_room.glb
    TEXTURES viking_room.png viking_room.ktx2 ../textures/texture.jpg ../textures/viking_room2.ktx2 
    LIBS glm::glm tinygltf::tinygltf KTX::ktx imgui spdlog::spdlog xxHash::xxhash yaml-cpp ImGuizmoLib msdf-atlas-gen box2d::box2d
)