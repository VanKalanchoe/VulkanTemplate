# VulkanTemplate
VulkanTutorialHPP Following official Vulkan tutorial and adding more
add #define VK_ENABLE_BETA_EXTENSIONS and in cmake compiledefinition VK_ENABLE_BETA_EXTENSIONS
add vk::KHRPortabilitySubsetExtensionName device extension instance
add instancecreateinfo flag .flags =  vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
add find_package(zstd CONFIG REQUIRED) cmake
add     if(TARGET zstd::libzstd_shared)
        target_link_libraries(${CHAPTER_NAME} zstd::libzstd_shared)
    elseif(TARGET zstd::libzstd_static)
        target_link_libraries(${CHAPTER_NAME} zstd::libzstd_static)
    endif() cmake
[install_dependencies_macos.sh](https://github.com/user-attachments/files/22501759/install_dependencies_macos.sh)
#!/bin/bash
set -e

echo "Installing dependencies for Vulkan Tutorial..."

# Define vcpkg root relative to this script
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
VCPKG_ROOT="$SCRIPT_DIR/../vendor/vcpkg"
export PATH="$VCPKG_ROOT:$PATH"

# Check if vcpkg exists
if [ ! -f "$VCPKG_ROOT/vcpkg" ]; then
    echo "vcpkg not found. Cloning..."
    mkdir -p "$(dirname "$VCPKG_ROOT")"
    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
    cd "$VCPKG_ROOT"
    ./bootstrap-vcpkg.sh
    cd "$SCRIPT_DIR"
fi

# Verify vcpkg works
if ! command -v vcpkg &> /dev/null; then
    echo "vcpkg installation failed or not on PATH."
    exit 1
fi

# Enable binary caching
export VCPKG_BINARY_SOURCES="clear;files,$TMPDIR/vcpkg-cache,readwrite"
mkdir -p "$TMPDIR/vcpkg-cache"

echo "Installing all dependencies..."
vcpkg install --triplet=x64-osx --x-manifest-root="$SCRIPT_DIR/.."

echo
echo "Don't forget to install MoltenVK (Vulkan on macOS) via Homebrew:"
echo "  brew install molten-vk"
echo
echo "All dependencies installed!"
echo "You can now build with CMake, e.g.:"
echo "  cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
echo "  cmake --build build"
# " add to cmake options -DCMAKE_TOOLCHAIN_FILE=/Users/ya/Desktop/Dev/vulkan/VulkanTemplate/vendor/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_C_FLAGS="--sysroot=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk" -DCMAKE_CXX_FLAGS="--sysroot=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk"
