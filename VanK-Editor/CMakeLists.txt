# Recursively include ALL folders under src
file(GLOB_RECURSE EDITOR_ALL_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.*"
)

add_executable(VanK-Editor)

target_sources(VanK-Editor PRIVATE ${EDITOR_ALL_FILES})

# Match VS filters to directory structure on disk
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES ${EDITOR_ALL_FILES})

target_link_libraries(VanK-Editor VanK)

target_include_directories(VanK-Editor PRIVATE src)

# Set working directory for VS debugger
set_target_properties(VanK-Editor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/VanK-Editor"
)