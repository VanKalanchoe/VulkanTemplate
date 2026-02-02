# Recursively include ALL folders under src
file(GLOB_RECURSE EDITOR_ALL_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.*"
)

add_executable(VanK-Editor)

target_sources(VanK-Editor PRIVATE ${EDITOR_ALL_FILES})

# Match VS filters to directory structure on disk
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES ${EDITOR_ALL_FILES})

target_link_libraries(VanK-Editor VanK)

target_include_directories(VanK-Editor PRIVATE src assets/shaders)

# Copy Resources folder one level above the exe
add_custom_command(TARGET VanK-Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/VanK-Editor/assets/Resources"
        "$<TARGET_FILE_DIR:VanK-Editor>/../Resources"
)

# Copy content folder next to the exe
add_custom_command(TARGET VanK-Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/VanK-Editor/assets/content"
        "$<TARGET_FILE_DIR:VanK-Editor>/content"
)

if(WIN32)
    target_link_libraries(VanK-Editor "${CMAKE_SOURCE_DIR}/VanK/Vendor/slang/lib/slang.lib")

    add_custom_command(TARGET VanK-Editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/VanK/Vendor/slang/bin/slang.dll"
            $<TARGET_FILE_DIR:VanK-Editor>
    )

    add_custom_command(TARGET VanK-Editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/VanK/Vendor/slang/bin/slang-glslang.dll"
            $<TARGET_FILE_DIR:VanK-Editor>
    )
endif()

if(APPLE)
    target_link_libraries(VanK-Editor "${CMAKE_SOURCE_DIR}/VanK/Vendor/slang/lib/libslang.dylib")

    add_custom_command(TARGET VanK-Editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/VanK/Vendor/slang/lib/libslang.dylib"
            $<TARGET_FILE_DIR:VanK-Editor>
    )

    add_custom_command(TARGET VanK-Editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/VanK/Vendor/slang/lib/libslang-glslang.dylib"
            $<TARGET_FILE_DIR:VanK-Editor>
    )
endif()

# Set working directory for VS debugger
set_target_properties(VanK-Editor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/VanK-Editor"
)