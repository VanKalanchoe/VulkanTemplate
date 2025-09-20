cmake_minimum_required (VERSION 3.29)

# Enable C++ module dependency scanning
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

project (VulkanTutorial)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/CMake")

find_package (glfw3 REQUIRED)
find_package (glm REQUIRED)
find_package (Vulkan REQUIRED)
find_package (tinyobjloader REQUIRED)
find_package (tinygltf REQUIRED)
find_package (KTX REQUIRED)

# set up Vulkan C++ module
add_library(VulkanCppModule)
add_library(Vulkan::cppm ALIAS VulkanCppModule)

target_compile_definitions(VulkanCppModule
        PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
)
target_include_directories(VulkanCppModule
        PRIVATE
        "${Vulkan_INCLUDE_DIR}"
)
target_link_libraries(VulkanCppModule
        PUBLIC
        Vulkan::Vulkan
)

set_target_properties(VulkanCppModule PROPERTIES CXX_STANDARD 20)

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

find_package(stb REQUIRED)
set(STB_INCLUDEDIR ${stb_INCLUDE_DIRS})

add_executable (glslang::validator IMPORTED)
find_program (GLSLANG_VALIDATOR "glslangValidator" HINTS $ENV{VULKAN_SDK}/bin REQUIRED)
set_property (TARGET glslang::validator PROPERTY IMPORTED_LOCATION "${GLSLANG_VALIDATOR}")
find_program(SLANGC_EXECUTABLE slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)

function (add_shaders_target TARGET)
  cmake_parse_arguments ("SHADER" "" "CHAPTER_NAME" "SOURCES" ${ARGN})
  set (SHADERS_DIR ${SHADER_CHAPTER_NAME}/shaders)
  add_custom_command (
    OUTPUT ${SHADERS_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR}
    )
  add_custom_command (
    OUTPUT ${SHADERS_DIR}/frag.spv ${SHADERS_DIR}/vert.spv
    COMMAND glslang::validator
    ARGS --target-env vulkan1.0 ${SHADER_SOURCES} --quiet
    WORKING_DIRECTORY ${SHADERS_DIR}
    DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
    COMMENT "Compiling Shaders"
    VERBATIM
    )
  add_custom_target (${TARGET} DEPENDS ${SHADERS_DIR}/frag.spv ${SHADERS_DIR}/vert.spv)
endfunction ()

function (add_slang_shader_target TARGET)
  cmake_parse_arguments ("SHADER" "" "CHAPTER_NAME" "SOURCES" ${ARGN})
  set (SHADERS_DIR ${SHADER_CHAPTER_NAME}/shaders)
  file(GLOB HAS_COMPUTE ${CHAPTER_SHADER}.comp)
  set (ENTRY_POINTS -entry vertMain -entry fragMain)
  if(HAS_COMPUTE)
    list(APPEND ENTRY_POINTS -entry compMain)
  endif()
  add_custom_command (
          OUTPUT ${SHADERS_DIR}
          COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR}
  )
  add_custom_command (
          OUTPUT  ${SHADERS_DIR}/slang.spv
          COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -target spirv -profile spirv_1_4+spvRayQueryKHR -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -o slang.spv
          WORKING_DIRECTORY ${SHADERS_DIR}
          DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
          COMMENT "Compiling Slang Shaders"
          VERBATIM
  )
  add_custom_target (${TARGET} DEPENDS ${SHADERS_DIR}/slang.spv)
endfunction()

function (add_chapter CHAPTER_NAME)
  cmake_parse_arguments (CHAPTER "" "SHADER" "LIBS;TEXTURES;MODELS" ${ARGN})
  add_executable (${CHAPTER_NAME} ${CHAPTER_NAME}.cpp)
  set_target_properties (${CHAPTER_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CHAPTER_NAME})
  set_target_properties (${CHAPTER_NAME} PROPERTIES CXX_STANDARD 20)
  target_link_libraries (${CHAPTER_NAME} Vulkan::cppm glfw)
  target_include_directories (${CHAPTER_NAME} PRIVATE ${STB_INCLUDEDIR})

  if(WIN32)
    if(${CMAKE_GENERATOR} MATCHES "Visual Studio.*")
      set_target_properties(${CHAPTER_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/${CHAPTER_NAME}")
    endif()
  endif()

  if (DEFINED CHAPTER_SHADER)
    set (CHAPTER_SHADER_TARGET ${CHAPTER_NAME}_shader)
    file (GLOB SHADER_SOURCES ${CHAPTER_SHADER}.frag ${CHAPTER_SHADER}.vert ${CHAPTER_SHADER}.comp)
    add_shaders_target (${CHAPTER_SHADER_TARGET} CHAPTER_NAME ${CHAPTER_NAME} SOURCES ${SHADER_SOURCES})
    add_dependencies (${CHAPTER_NAME} ${CHAPTER_SHADER_TARGET})

    set (CHAPTER_SHADER_SLANG_TARGET ${CHAPTER_NAME}_slang_shader)
    file (GLOB SHADER_SLANG_SOURCES ${CHAPTER_SHADER}.slang)
    if(SHADER_SLANG_SOURCES)
      add_slang_shader_target( ${CHAPTER_SHADER_SLANG_TARGET} CHAPTER_NAME ${CHAPTER_NAME} SOURCES ${SHADER_SLANG_SOURCES})
      add_dependencies(${CHAPTER_NAME} ${CHAPTER_SHADER_SLANG_TARGET})
    endif()
  endif ()
  if (DEFINED CHAPTER_LIBS)
    target_link_libraries (${CHAPTER_NAME} ${CHAPTER_LIBS})
  endif ()
  if (DEFINED CHAPTER_MODELS)
    list(TRANSFORM CHAPTER_MODELS PREPEND "${CMAKE_SOURCE_DIR}/assets/")
    file (COPY ${CHAPTER_MODELS} DESTINATION ${CMAKE_BINARY_DIR}/${CHAPTER_NAME}/models)
  endif ()
  if (DEFINED CHAPTER_TEXTURES)
    list(TRANSFORM CHAPTER_TEXTURES PREPEND "${CMAKE_SOURCE_DIR}/assets/")
    file (COPY ${CHAPTER_TEXTURES} DESTINATION ${CMAKE_BINARY_DIR}/${CHAPTER_NAME}/textures)
  endif ()
endfunction ()

add_chapter (main)