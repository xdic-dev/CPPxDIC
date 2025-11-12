#!/bin/bash

# Build script for CPPXDIC
# C++ equivalent of Matlab xDIC using ncorr library

set -e  # Exit on any error

echo "Building CPPXDIC - C++ Digital Image Correlation Library"
echo "======================================================"

# Check if ncorr library exists
NCORR_LIB="Tools/CppNCorr/lib/libncorr.a"
NCORR_INCLUDE="Tools/CppNCorr/include"

if [ ! -f "$NCORR_LIB" ]; then
    echo "Error: ncorr library not found at $NCORR_LIB"
    echo "Please build the ncorr library first:"
    echo "  cd ../ncorr_2D_cpp-master"
    echo "  mkdir -p build && cd build"
    echo "  cmake .."
    echo "  make"
    exit 1
fi

if [ ! -d "$NCORR_INCLUDE" ]; then
    echo "Error: ncorr include directory not found at $NCORR_INCLUDE"
    exit 1
fi

echo "✓ ncorr library found"

# Check for OpenCV
if ! pkg-config --exists opencv4; then
    if ! pkg-config --exists opencv; then
        echo "Error: OpenCV not found. Please install OpenCV development libraries."
        echo "  Ubuntu/Debian: sudo apt-get install libopencv-dev"
        echo "  macOS: brew install opencv"
        exit 1
    fi
fi

echo "✓ OpenCV found"

# Check for FFTW3 (optional check since we use find_library)
if pkg-config --exists fftw3; then
    echo "✓ FFTW3 found via pkg-config"
elif [ -f "/opt/homebrew/lib/libfftw3.dylib" ] || [ -f "/usr/local/lib/libfftw3.so" ]; then
    echo "✓ FFTW3 library found"
else
    echo "Warning: FFTW3 not found via pkg-config, but CMake will try to find it"
    echo "  If build fails, install with: brew install fftw (macOS) or apt-get install libfftw3-dev (Ubuntu)"
fi

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring with CMake..."
if cmake ..; then
    echo "✓ CMake configuration successful"
else
    echo "❌ CMake configuration failed"
    echo "Trying manual build..."
    cd ..
    
    # Manual build fallback
    echo "Building manually..."
    g++ -std=c++17 -O2 -Wall -Wextra \
        -Iinclude -ITools/CppNCorr/include -isystem /opt/homebrew/include \
        src/main.cpp src/config.cpp src/dic_analysis.cpp src/utils.cpp \
        $(pkg-config --cflags --libs opencv4 2>/dev/null || pkg-config --cflags --libs opencv) \
        -LTools/CppNCorr/lib -lncorr \
        -L/opt/homebrew/lib -lfftw3 \
        -lspqr -lcholmod -lsuitesparseconfig -lamd -lcolamd \
        -llapack -lblas -pthread \
        -o cppxdic
    
    if [ $? -eq 0 ]; then
        echo "✓ Manual build successful"
        echo "Executable created: ./cppxdic"
        exit 0
    else
        echo "❌ Manual build failed"
        exit 1
    fi
fi

# Build with make
echo "Building with make..."
if make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4); then
    echo "✓ Build successful"
    echo "Executable created: ./cppxdic"
else
    echo "❌ Build failed"
    exit 1
fi

echo ""
echo "Build completed successfully!"
echo "To run: cd build && ./cppxdic"
