#!/bin/bash

# Add local vcpkg folder to PATH temporarily
VCPKG_ROOT="$(dirname "$0")/../VanK/vendor/vcpkg"
export PATH="$VCPKG_ROOT:$PATH"

# Run CMake with vcpkg toolchain
cmake -B "../build" -S ".." -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

# Uncomment to build automatically
# cmake --build ../build

# Pause (wait for user input)
read -p "Press [Enter] to continue..."
