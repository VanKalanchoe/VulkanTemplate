:: Add local vcpkg folder to PATH temporarily
set "VCPKG_ROOT=%~dp0..\vendor\vcpkg"
set "PATH=%VCPKG_ROOT%;%PATH%"

cmake -B ../build -S .. -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
:: cmake --build build

pause