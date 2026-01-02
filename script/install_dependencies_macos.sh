#!/bin/bash

echo "Installing dependencies for Vulkan Tutorial..."

# Add local vcpkg folder to PATH temporarily
VCPKG_ROOT="$(dirname "$0")/../VanK/vendor/vcpkg"
export PATH="$VCPKG_ROOT:$PATH"

# Check if vcpkg is installed
#if ! command -v vcpkg &> /dev/null; then
#    echo "vcpkg not found. Please install vcpkg first."
#    echo "Visit https://github.com/microsoft/vcpkg for installation instructions."
#    echo "Typically, you would:"
#    echo "1. git clone https://github.com/Microsoft/vcpkg.git"
#    echo "2. cd vcpkg"
#    echo "3. ./bootstrap-vcpkg.sh"
#    echo "4. Add vcpkg to your PATH"
#    exit 1
#fi

# Check if vcpkg is installed
if ! command -v vcpkg &> /dev/null; then
    echo "vcpkg not found. Installing automatically..."

    # Clone vcpkg into the vendor folder
    VCPKG_DIR="$(dirname "$0")/../VanK/vendor/vcpkg"
    if [ ! -d "$VCPKG_DIR" ]; then
        git clone https://github.com/Microsoft/vcpkg.git "$VCPKG_DIR"
    fi

    # Bootstrap vcpkg
    echo "Bootstrapping vcpkg..."
    cd "$VCPKG_DIR" || exit 1
    ./bootstrap-vcpkg.sh

    # Return to script directory
    cd - > /dev/null

    # Add vcpkg to PATH for current session
    export PATH="$VCPKG_DIR:$PATH"

    echo "vcpkg installed successfully!"
fi

#slang
SLANG_VERSION="2025.18.1"
SLANG_URL="https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}-macos-aarch64.zip"
DEST_DIR="../VanK/vendor/slang"

# Only download and extract if DEST_DIR does NOT exist
if [ ! -d "$DEST_DIR" ]; then
    echo "Downloading Slang $SLANG_VERSION..."
    curl -L "$SLANG_URL" -o slang.zip

    echo "Creating destination folder..."
    mkdir -p "$DEST_DIR"

    echo "Extracting zip AS-IS into $DEST_DIR..."
    # Use unzip on macOS/Linux for ZIP files
    unzip -q slang.zip -d "$DEST_DIR"

    rm slang.zip
    echo "Slang downloaded and extracted into '$DEST_DIR'."
else
    echo "Slang already installed in '$DEST_DIR', skipping download and extraction."
fi

#-------

# Enable binary caching for vcpkg
echo "Enabling binary caching for vcpkg..."
export VCPKG_BINARY_SOURCES="clear;files,$TMPDIR/vcpkg-cache,readwrite"

# Create cache directory if it doesn't exist
mkdir -p "$TMPDIR/vcpkg-cache"

# Install all dependencies at once using vcpkg with parallel installation
echo "Installing all dependencies..."
vcpkg install --triplet=x64-osx --x-manifest-root="$(dirname "$0")/.." \
    --feature-flags=binarycaching,manifests --x-install-root="${VCPKG_INSTALLATION_ROOT:-$HOME/vcpkg_installed}/installed"

# Remind about Vulkan SDK
echo
echo "Don't forget to install the Vulkan SDK from https://vulkan.lunarg.com/"
echo

echo "All dependencies have been installed successfully!"
echo "You can now use CMake to build your Vulkan project."
echo
echo "Example CMake command:"
echo "cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[path/to/vcpkg]/scripts/buildsystems/vcpkg.cmake"
echo "cmake --build build"

exit 0
