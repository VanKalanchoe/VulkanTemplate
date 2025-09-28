set(SOURCES
    src/Main.cpp
    src/EditorLayer.h
    src/EditorLayer.cpp
)

add_executable(VanK-Editor)

target_sources(VanK-Editor PRIVATE ${SOURCES})

# Match VS filters to directory structure on disk
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES})

target_link_libraries(VanK-Editor VanK)

target_include_directories(VanK-Editor PRIVATE src)

# Set working directory for VS debugger
set_target_properties(VanK-Editor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/VanK-Editor"
)