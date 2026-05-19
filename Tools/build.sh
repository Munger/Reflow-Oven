#!/bin/sh
# Build script for ReflowOven firmware using STM32CubeIDE's bundled tools.
# Usage: build.sh [ninja args]
#   --reconfigure   Re-run cmake before building

CUBE="/Applications/STMicroelectronics/STM32CubeIDE.app/Contents/Eclipse/plugins"
CMAKE="$CUBE/com.st.stm32cube.ide.mcu.externaltools.cmake.macosaarch64_1.0.1.202603112338/tools/bin/cmake"
NINJA="$CUBE/com.st.stm32cube.ide.mcu.externaltools.ninja.macosaarch64_1.0.0.202601242230/tools/bin/ninja"

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$REPO/build/Debug"
TOOLCHAIN="$HOME/Library/Application Support/stm32cube/bundles/gnu-tools-for-stm32/14.3.1+st.2/bin"
export PATH="$TOOLCHAIN:$PATH"

if [ "$1" = "--reconfigure" ]; then
    shift
    "$CMAKE" -S "$REPO" -B "$BUILD" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/gcc-arm-none-eabi.cmake" \
        -DCMAKE_BUILD_TYPE=Debug || exit 1
fi

exec "$NINJA" -C "$BUILD" "$@"
