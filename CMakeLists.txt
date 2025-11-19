cmake_minimum_required(VERSION 3.16)
project(cppxdic)

# Set C++ standard (use C++17 for filesystem support)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set include directories (following ncorr pattern)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isystem /opt/homebrew/include")

# Include directories
include_directories(include)
include_directories(Tools/CppNCorr/include)

# Add executable with new source files
add_executable(cppxdic
    src/main.cpp
    src/config.cpp
    src/dic_analysis.cpp
    src/dic_structures.cpp
    src/utils.cpp
    src/image_processor.cpp
    src/roi_manager.cpp
    src/mat_writer.cpp
    src/step_d_workflow.cpp
    src/strain_computation.cpp
    src/mat_reader.cpp
    src/delaunay_triangulation.cpp
    src/surface_stitching.cpp
    src/temporal_filter.cpp
    src/face_isotropy.cpp
)

# Find OpenCV (following ncorr pattern)
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})
target_link_libraries(cppxdic ${OpenCV_LIBS})

# Add ncorr library
find_library(NCORR_LIBRARY NAMES ncorr PATHS Tools/CppNCorr/lib)
target_link_libraries(cppxdic ${NCORR_LIBRARY})

# Add OpenCV libraries explicitly (following ncorr pattern)
find_library(OPENCV_LIBRARY1 NAMES opencv_core)
target_link_libraries(cppxdic ${OPENCV_LIBRARY1})

find_library(OPENCV_LIBRARY2 NAMES opencv_highgui)
target_link_libraries(cppxdic ${OPENCV_LIBRARY2})

find_library(OPENCV_LIBRARY3 NAMES opencv_imgcodecs)
target_link_libraries(cppxdic ${OPENCV_LIBRARY3})

find_library(OPENCV_LIBRARY4 NAMES opencv_imgproc)
target_link_libraries(cppxdic ${OPENCV_LIBRARY4})

find_library(OPENCV_LIBRARY5 NAMES opencv_videoio)
target_link_libraries(cppxdic ${OPENCV_LIBRARY5})

# Add fftw library
find_library(FFTW_LIBRARY NAMES fftw3)
target_link_libraries(cppxdic ${FFTW_LIBRARY})

# Add matio library for reading/writing MATLAB .mat files
find_library(MATIO_LIBRARY NAMES matio)
if(MATIO_LIBRARY)
    target_link_libraries(cppxdic ${MATIO_LIBRARY})
    message(STATUS "Found MATIO library: ${MATIO_LIBRARY}")
    # Also add MATIO include directory if available
    find_path(MATIO_INCLUDE_DIR matio.h
      HINTS /opt/homebrew/include /usr/local/include /usr/include
    )
    if(MATIO_INCLUDE_DIR)
        target_include_directories(cppxdic PRIVATE ${MATIO_INCLUDE_DIR})
        message(STATUS "Found MATIO include: ${MATIO_INCLUDE_DIR}")
    else()
        message(WARNING "MATIO include directory not found. If build fails, install headers (brew install matio).")
    endif()
else()
    message(FATAL_ERROR "MATIO library not found. Please install it: brew install libmatio")
endif()

# Add HDF5 library (required for MATIO v7.3 support)
find_package(HDF5 COMPONENTS C)
if(HDF5_FOUND)
    target_include_directories(cppxdic PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(cppxdic ${HDF5_C_LIBRARIES})
    message(STATUS "Found HDF5: ${HDF5_C_LIBRARIES}")
else()
    message(WARNING "HDF5 not found. MAT v7.3 (HDF5) format may not work. Install with: brew install hdf5")
endif()

# Add CGAL library (for Delaunay triangulation matching MATLAB)
find_package(CGAL REQUIRED)
if(CGAL_FOUND)
    target_link_libraries(cppxdic CGAL::CGAL)
    message(STATUS "Found CGAL: ${CGAL_VERSION}")
else()
    message(FATAL_ERROR "CGAL not found. Please install it: brew install cgal")
endif()

# Add Eigen library (for matrix operations in strain computation)
find_package(Eigen3 REQUIRED)
if(Eigen3_FOUND)
    # Prefer imported target when available
    if(TARGET Eigen3::Eigen)
        target_link_libraries(cppxdic Eigen3::Eigen)
        message(STATUS "Found Eigen3 target: Eigen3::Eigen")
    endif()
    # Also add include dirs for header-only usage depending on variable name
    if(DEFINED EIGEN3_INCLUDE_DIRS)
        target_include_directories(cppxdic PRIVATE ${EIGEN3_INCLUDE_DIRS})
        message(STATUS "Eigen3 include dirs: ${EIGEN3_INCLUDE_DIRS}")
    elseif(DEFINED EIGEN3_INCLUDE_DIR)
        target_include_directories(cppxdic PRIVATE ${EIGEN3_INCLUDE_DIR})
        message(STATUS "Eigen3 include dir: ${EIGEN3_INCLUDE_DIR}")
    endif()
else()
    message(FATAL_ERROR "Eigen3 not found. Please install it: brew install eigen")
endif()

# Add suite sparse libraries
find_library(SUITESPARSE_LIBRARY1 NAMES spqr)
target_link_libraries(cppxdic ${SUITESPARSE_LIBRARY1})

find_library(SUITESPARSE_LIBRARY2 NAMES cholmod)
target_link_libraries(cppxdic ${SUITESPARSE_LIBRARY2})

find_library(SUITESPARSE_LIBRARY3 NAMES suitesparseconfig)
target_link_libraries(cppxdic ${SUITESPARSE_LIBRARY3})

find_library(SUITESPARSE_LIBRARY4 NAMES amd)
target_link_libraries(cppxdic ${SUITESPARSE_LIBRARY4})

find_library(SUITESPARSE_LIBRARY5 NAMES colamd)
target_link_libraries(cppxdic ${SUITESPARSE_LIBRARY5})

# Add lapack library
find_library(LAPACK_LIBRARY NAMES lapack)
target_link_libraries(cppxdic ${LAPACK_LIBRARY})

# Add blas library
find_library(BLAS_LIBRARY NAMES blas)
target_link_libraries(cppxdic ${BLAS_LIBRARY})

# Add pthreads
find_package(Threads)
target_link_libraries(cppxdic ${CMAKE_THREAD_LIBS_INIT})

# Add OpenMP support (required by ncorr library) - following ncorr's approach
if(APPLE)
    # Check for Homebrew OpenMP installation
    if(EXISTS "/opt/homebrew/opt/libomp")
        set(OpenMP_C_FLAGS "-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include")
        set(OpenMP_C_LIB_NAMES "omp")
        set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include")
        set(OpenMP_CXX_LIB_NAMES "omp")
        set(OpenMP_omp_LIBRARY "/opt/homebrew/opt/libomp/lib/libomp.dylib")
    elseif(EXISTS "/usr/local/opt/libomp")
        set(OpenMP_C_FLAGS "-Xpreprocessor -fopenmp -I/usr/local/opt/libomp/include")
        set(OpenMP_C_LIB_NAMES "omp")
        set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I/usr/local/opt/libomp/include")
        set(OpenMP_CXX_LIB_NAMES "omp")
        set(OpenMP_omp_LIBRARY "/usr/local/opt/libomp/lib/libomp.dylib")
    endif()
endif()

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found - enabling parallel support")
    target_link_libraries(cppxdic OpenMP::OpenMP_CXX)
else()
    message(WARNING "OpenMP not found - please install: brew install libomp")
endif()

# Compiler flags
target_compile_options(cppxdic PRIVATE -Wall -Wextra -O2)

# Create directories for output
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/save)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/video)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/images)

# Copy test images if they exist
if(EXISTS ${CMAKE_SOURCE_DIR}/Tools/CppNCorr/test/examples/ohtcfrp/images)
    file(COPY ${CMAKE_SOURCE_DIR}/Tools/CppNCorr/test/examples/ohtcfrp/images 
         DESTINATION ${CMAKE_BINARY_DIR})
endif()

# Installation
install(TARGETS cppxdic DESTINATION bin)

# Print configuration info
message(STATUS "Building cppxdic - C++ equivalent of Matlab xDIC")
message(STATUS "Using ncorr library from: ${CMAKE_SOURCE_DIR}/Tools/CppNCorr/lib/libncorr.a")
message(STATUS "OpenCV version: ${OpenCV_VERSION}")

# Find and link nlohmann/json library for ncorr_test_json, ncorr_test_verif
find_package(nlohmann_json CONFIG)
if(nlohmann_json_FOUND)
target_link_libraries(cppxdic nlohmann_json::nlohmann_json)
else()
  # If package not found via find_package, try to find the header-only library
  find_path(NLOHMANN_JSON_INCLUDE_DIRS "nlohmann/json.hpp" 
            PATHS /usr/include /usr/local/include /opt/homebrew/include)
  if(NLOHMANN_JSON_INCLUDE_DIRS)
    include_directories(${NLOHMANN_JSON_INCLUDE_DIRS})
    message(STATUS "Found nlohmann/json include directory: ${NLOHMANN_JSON_INCLUDE_DIRS}")
  else()
    message(FATAL_ERROR "nlohmann/json library not found. Please install it using your package manager.")
  endif()
endif()

