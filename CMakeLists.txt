cmake_minimum_required (VERSION 3.29)

project (VanKCore)

set(CMAKE_CXX_STANDARD 23)

# Add msdf-atlas-gen submodule
add_subdirectory(VanK/Vendor/msdf-atlas-gen SYSTEM)

# Force msdf-atlas-gen libraries to use /MD or /MDd
set_property(TARGET msdf-atlas-gen PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set_property(TARGET msdfgen-core   PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set_property(TARGET msdfgen-ext    PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set_property(TARGET msdf-atlas-gen-standalone PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Projects
add_subdirectory(VanK)
add_subdirectory(VanK-Editor)