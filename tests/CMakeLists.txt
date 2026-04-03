cmake_minimum_required(VERSION 3.16)
project(CPPxDIC_Tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Set include directories (following parent project pattern)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isystem /opt/homebrew/include")

# Include directories from parent project
include_directories(
    ${CMAKE_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/../Tools/CppNCorr/include
)

# Find OpenCV (following parent project pattern)
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# Find OpenMP early (required for linking with ncorr)
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
    message(STATUS "OpenMP found - will enable for test programs")
endif()

# Find pthreads early
find_package(Threads)

# Find Eigen3 early (required by dic_structures.h -> strain_computation.h)
find_package(Eigen3 REQUIRED)
if(Eigen3_FOUND)
    if(TARGET Eigen3::Eigen)
        message(STATUS "Found Eigen3 target: Eigen3::Eigen")
    endif()
endif()

# Find CGAL (required by delaunay_triangulation.cpp)
find_package(CGAL REQUIRED)
if(CGAL_FOUND)
    message(STATUS "Found CGAL: ${CGAL_VERSION}")
endif()

# Test 1: test_ncorr_bin - Analyze .bin files (now supports both ncorr and DIC3Dcombined formats)
add_executable(test_ncorr_bin
    src/test_ncorr_bin.cpp
    ${CMAKE_SOURCE_DIR}/../src/dic_structures.cpp
)

# Link ncorr library and dependencies (needed for loading ncorr::DIC_analysis_output files)
find_library(NCORR_LIBRARY_BIN NAMES ncorr PATHS ${CMAKE_SOURCE_DIR}/../Tools/CppNCorr/lib)
if(NCORR_LIBRARY_BIN)
    target_link_libraries(test_ncorr_bin ${NCORR_LIBRARY_BIN})
endif()

target_link_libraries(test_ncorr_bin ${OpenCV_LIBS})

# Add FFTW (required by ncorr)
find_library(FFTW_LIBRARY_BIN NAMES fftw3)
if(FFTW_LIBRARY_BIN)
    target_link_libraries(test_ncorr_bin ${FFTW_LIBRARY_BIN})
endif()

# Add SuiteSparse libraries (required by ncorr)
find_library(SUITESPARSE_SPQR_BIN NAMES spqr)
if(SUITESPARSE_SPQR_BIN)
    target_link_libraries(test_ncorr_bin ${SUITESPARSE_SPQR_BIN})
endif()

find_library(SUITESPARSE_CHOLMOD_BIN NAMES cholmod)
if(SUITESPARSE_CHOLMOD_BIN)
    target_link_libraries(test_ncorr_bin ${SUITESPARSE_CHOLMOD_BIN})
endif()

find_library(SUITESPARSE_CONFIG_BIN NAMES suitesparseconfig)
if(SUITESPARSE_CONFIG_BIN)
    target_link_libraries(test_ncorr_bin ${SUITESPARSE_CONFIG_BIN})
endif()

find_library(SUITESPARSE_AMD_BIN NAMES amd)
if(SUITESPARSE_AMD_BIN)
    target_link_libraries(test_ncorr_bin ${SUITESPARSE_AMD_BIN})
endif()

find_library(SUITESPARSE_COLAMD_BIN NAMES colamd)
if(SUITESPARSE_COLAMD_BIN)
    target_link_libraries(test_ncorr_bin ${SUITESPARSE_COLAMD_BIN})
endif()

# Add BLAS and LAPACK (required by ncorr)
find_library(LAPACK_LIBRARY_BIN NAMES lapack)
if(LAPACK_LIBRARY_BIN)
    target_link_libraries(test_ncorr_bin ${LAPACK_LIBRARY_BIN})
endif()

find_library(BLAS_LIBRARY_BIN NAMES blas)
if(BLAS_LIBRARY_BIN)
    target_link_libraries(test_ncorr_bin ${BLAS_LIBRARY_BIN})
endif()

# Add OpenMP (required by ncorr)
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_ncorr_bin OpenMP::OpenMP_CXX)
endif()

# Add pthreads
target_link_libraries(test_ncorr_bin ${CMAKE_THREAD_LIBS_INIT})

# Add Eigen3 (required by dic_structures.h -> strain_computation.h)
if(TARGET Eigen3::Eigen)
    target_link_libraries(test_ncorr_bin Eigen3::Eigen)
endif()

# Test 2: test_ncorr_to_mat - Convert .bin to .mat
add_executable(test_ncorr_to_mat
    src/test_ncorr_to_mat.cpp
    ${CMAKE_SOURCE_DIR}/../src/dic_structures.cpp
    ${CMAKE_SOURCE_DIR}/../src/mat_writer.cpp
    ${CMAKE_SOURCE_DIR}/../src/io/mat/matio_helpers.cpp
    ${CMAKE_SOURCE_DIR}/../src/io/mat/mat_ncorr_writer.cpp
    ${CMAKE_SOURCE_DIR}/../src/io/mat/mat_results_writer.cpp
)

# Add matio library (following parent project pattern)
find_library(MATIO_LIBRARY NAMES matio)
if(MATIO_LIBRARY)
    target_link_libraries(test_ncorr_to_mat ${MATIO_LIBRARY})
    message(STATUS "Found MATIO library: ${MATIO_LIBRARY}")
    # Also add MATIO include directory if available
    find_path(MATIO_INCLUDE_DIR matio.h
      HINTS /opt/homebrew/include /usr/local/include /usr/include
    )
    if(MATIO_INCLUDE_DIR)
        target_include_directories(test_ncorr_to_mat PRIVATE ${MATIO_INCLUDE_DIR})
        message(STATUS "Found MATIO include: ${MATIO_INCLUDE_DIR}")
    endif()
else()
    message(FATAL_ERROR "MATIO library not found. Please install it: brew install libmatio")
endif()

# Add HDF5 library (required for MATIO v7.3 support)
find_package(HDF5 COMPONENTS C)
if(HDF5_FOUND)
    target_include_directories(test_ncorr_to_mat PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(test_ncorr_to_mat ${HDF5_C_LIBRARIES})
    message(STATUS "Found HDF5: ${HDF5_C_LIBRARIES}")
else()
    message(WARNING "HDF5 not found. MAT v7.3 (HDF5) format may not work. Install with: brew install hdf5")
endif()

# Add Eigen library (required by mat_writer.h for matrix operations)
find_package(Eigen3 REQUIRED)
if(Eigen3_FOUND)
    # Prefer imported target when available
    if(TARGET Eigen3::Eigen)
        target_link_libraries(test_ncorr_to_mat Eigen3::Eigen)
        message(STATUS "Found Eigen3 target: Eigen3::Eigen")
    endif()
    # Also add include dirs for header-only usage
    if(DEFINED EIGEN3_INCLUDE_DIRS)
        target_include_directories(test_ncorr_to_mat PRIVATE ${EIGEN3_INCLUDE_DIRS})
        message(STATUS "Eigen3 include dirs: ${EIGEN3_INCLUDE_DIRS}")
    elseif(DEFINED EIGEN3_INCLUDE_DIR)
        target_include_directories(test_ncorr_to_mat PRIVATE ${EIGEN3_INCLUDE_DIR})
        message(STATUS "Eigen3 include dir: ${EIGEN3_INCLUDE_DIR}")
    endif()
else()
    message(FATAL_ERROR "Eigen3 not found. Please install it: brew install eigen")
endif()

# Add ncorr library (following parent project pattern)
find_library(NCORR_LIBRARY NAMES ncorr PATHS ${CMAKE_SOURCE_DIR}/../Tools/CppNCorr/lib)
if(NCORR_LIBRARY)
    target_link_libraries(test_ncorr_to_mat ${NCORR_LIBRARY})
    message(STATUS "Found ncorr library: ${NCORR_LIBRARY}")
else()
    message(FATAL_ERROR "ncorr library not found at ${CMAKE_SOURCE_DIR}/../Tools/CppNCorr/lib")
endif()

# Link OpenCV to test_ncorr_to_mat
target_link_libraries(test_ncorr_to_mat ${OpenCV_LIBS})

# Add fftw library (needed by ncorr)
find_library(FFTW_LIBRARY NAMES fftw3)
if(FFTW_LIBRARY)
    target_link_libraries(test_ncorr_to_mat ${FFTW_LIBRARY})
endif()

# Add suite sparse libraries (needed by ncorr)
find_library(SUITESPARSE_LIBRARY1 NAMES spqr)
target_link_libraries(test_ncorr_to_mat ${SUITESPARSE_LIBRARY1})

find_library(SUITESPARSE_LIBRARY2 NAMES cholmod)
target_link_libraries(test_ncorr_to_mat ${SUITESPARSE_LIBRARY2})

find_library(SUITESPARSE_LIBRARY3 NAMES suitesparseconfig)
target_link_libraries(test_ncorr_to_mat ${SUITESPARSE_LIBRARY3})

find_library(SUITESPARSE_LIBRARY4 NAMES amd)
target_link_libraries(test_ncorr_to_mat ${SUITESPARSE_LIBRARY4})

find_library(SUITESPARSE_LIBRARY5 NAMES colamd)
target_link_libraries(test_ncorr_to_mat ${SUITESPARSE_LIBRARY5})

# Add lapack library
find_library(LAPACK_LIBRARY NAMES lapack)
target_link_libraries(test_ncorr_to_mat ${LAPACK_LIBRARY})

# Add blas library
find_library(BLAS_LIBRARY NAMES blas)
target_link_libraries(test_ncorr_to_mat ${BLAS_LIBRARY})

# Add pthreads (already found earlier)
target_link_libraries(test_ncorr_to_mat ${CMAKE_THREAD_LIBS_INIT})

# Add OpenMP (already found earlier)
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_ncorr_to_mat OpenMP::OpenMP_CXX)
endif()

# Test 3: test_mat_comparator - Compare MAT files
add_executable(test_mat_comparator
    src/test_mat_comparator.cpp
)

# Link matio library
target_link_libraries(test_mat_comparator ${MATIO_LIBRARY})

# Link HDF5 library
if(HDF5_FOUND)
    target_include_directories(test_mat_comparator PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(test_mat_comparator ${HDF5_C_LIBRARIES})
endif()

# Test 4: test_mat_writers - Unit tests for MAT file writers
add_executable(test_mat_writers
    src/test_mat_writers.cpp
    ${CMAKE_SOURCE_DIR}/../src/dic_structures.cpp
    ${CMAKE_SOURCE_DIR}/../src/mat_writer.cpp
    ${CMAKE_SOURCE_DIR}/../src/io/mat/matio_helpers.cpp
    ${CMAKE_SOURCE_DIR}/../src/io/mat/mat_ncorr_writer.cpp
    ${CMAKE_SOURCE_DIR}/../src/io/mat/mat_results_writer.cpp
)

# Link matio library
target_link_libraries(test_mat_writers ${MATIO_LIBRARY})

# Link HDF5 library
if(HDF5_FOUND)
    target_include_directories(test_mat_writers PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(test_mat_writers ${HDF5_C_LIBRARIES})
endif()

# Link Eigen3
if(TARGET Eigen3::Eigen)
    target_link_libraries(test_mat_writers Eigen3::Eigen)
endif()

# Link ncorr library
if(NCORR_LIBRARY)
    target_link_libraries(test_mat_writers ${NCORR_LIBRARY})
endif()

# Link OpenCV
target_link_libraries(test_mat_writers ${OpenCV_LIBS})

# Link other dependencies (needed by ncorr)
if(FFTW_LIBRARY)
    target_link_libraries(test_mat_writers ${FFTW_LIBRARY})
endif()
target_link_libraries(test_mat_writers ${SUITESPARSE_LIBRARY1})
target_link_libraries(test_mat_writers ${SUITESPARSE_LIBRARY2})
target_link_libraries(test_mat_writers ${SUITESPARSE_LIBRARY3})
target_link_libraries(test_mat_writers ${SUITESPARSE_LIBRARY4})
target_link_libraries(test_mat_writers ${SUITESPARSE_LIBRARY5})
target_link_libraries(test_mat_writers ${LAPACK_LIBRARY})
target_link_libraries(test_mat_writers ${BLAS_LIBRARY})
target_link_libraries(test_mat_writers ${CMAKE_THREAD_LIBS_INIT})
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_mat_writers OpenMP::OpenMP_CXX)
endif()

# Test 5: test_var_test2 - Test StepD workflow intermediate outputs
add_executable(test_var_test2
    src/test_var_test2.cpp
)

# Link matio library
target_link_libraries(test_var_test2 ${MATIO_LIBRARY})

# Link HDF5 library
if(HDF5_FOUND)
    target_include_directories(test_var_test2 PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(test_var_test2 ${HDF5_C_LIBRARIES})
endif()

# Link OpenCV
target_link_libraries(test_var_test2 ${OpenCV_LIBS})

# Test 6: test_var_parity - Parity test for StepD workflow vs MATLAB
add_executable(test_var_parity
    src/test_var_parity.cpp
    ${CMAKE_SOURCE_DIR}/../src/config.cpp
    ${CMAKE_SOURCE_DIR}/../src/image_processor.cpp
    ${CMAKE_SOURCE_DIR}/../src/roi_manager.cpp
    ${CMAKE_SOURCE_DIR}/../src/utils.cpp
    ${CMAKE_SOURCE_DIR}/../src/dic_structures.cpp
    ${CMAKE_SOURCE_DIR}/../src/mat_reader.cpp
)

# Link matio library
target_link_libraries(test_var_parity ${MATIO_LIBRARY})

# Link HDF5 library
if(HDF5_FOUND)
    target_include_directories(test_var_parity PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(test_var_parity ${HDF5_C_LIBRARIES})
endif()

# Link OpenCV
target_link_libraries(test_var_parity ${OpenCV_LIBS})

# Link ncorr library (needed for DIC_analysis_output::load)
if(NCORR_LIBRARY)
    target_link_libraries(test_var_parity ${NCORR_LIBRARY})
endif()

# Link Eigen3
if(TARGET Eigen3::Eigen)
    target_link_libraries(test_var_parity Eigen3::Eigen)
endif()

# Link other dependencies (needed by ncorr)
if(FFTW_LIBRARY)
    target_link_libraries(test_var_parity ${FFTW_LIBRARY})
endif()
target_link_libraries(test_var_parity ${SUITESPARSE_LIBRARY1})
target_link_libraries(test_var_parity ${SUITESPARSE_LIBRARY2})
target_link_libraries(test_var_parity ${SUITESPARSE_LIBRARY3})
target_link_libraries(test_var_parity ${SUITESPARSE_LIBRARY4})
target_link_libraries(test_var_parity ${SUITESPARSE_LIBRARY5})
target_link_libraries(test_var_parity ${LAPACK_LIBRARY})
target_link_libraries(test_var_parity ${BLAS_LIBRARY})
target_link_libraries(test_var_parity ${CMAKE_THREAD_LIBS_INIT})
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_var_parity OpenMP::OpenMP_CXX)
endif()

# Test 7: test_pipeline_validation - Integration test for complete DIC pipeline
add_executable(test_pipeline_validation
    src/test_pipeline_validation.cpp
)

# Link matio library
target_link_libraries(test_pipeline_validation ${MATIO_LIBRARY})

# Link HDF5 library
if(HDF5_FOUND)
    target_include_directories(test_pipeline_validation PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(test_pipeline_validation ${HDF5_C_LIBRARIES})
endif()

# Test 8: test_deformation_validation - Unit test for TCPE deformation algorithm
add_executable(test_deformation_validation
    src/test_deformation_validation.cpp
    ${CMAKE_SOURCE_DIR}/../src/strain_computation.cpp
    ${CMAKE_SOURCE_DIR}/../src/dic_structures.cpp
)

# Link matio library
target_link_libraries(test_deformation_validation ${MATIO_LIBRARY})

# Link HDF5 library
if(HDF5_FOUND)
    target_include_directories(test_deformation_validation PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(test_deformation_validation ${HDF5_C_LIBRARIES})
endif()

# Link Eigen3
if(TARGET Eigen3::Eigen)
    target_link_libraries(test_deformation_validation Eigen3::Eigen)
endif()

# Link ncorr library
if(NCORR_LIBRARY)
    target_link_libraries(test_deformation_validation ${NCORR_LIBRARY})
endif()

# Link OpenCV
target_link_libraries(test_deformation_validation ${OpenCV_LIBS})

# Link other dependencies
if(FFTW_LIBRARY)
    target_link_libraries(test_deformation_validation ${FFTW_LIBRARY})
endif()
target_link_libraries(test_deformation_validation ${SUITESPARSE_LIBRARY1})
target_link_libraries(test_deformation_validation ${SUITESPARSE_LIBRARY2})
target_link_libraries(test_deformation_validation ${SUITESPARSE_LIBRARY3})
target_link_libraries(test_deformation_validation ${SUITESPARSE_LIBRARY4})
target_link_libraries(test_deformation_validation ${SUITESPARSE_LIBRARY5})
target_link_libraries(test_deformation_validation ${LAPACK_LIBRARY})
target_link_libraries(test_deformation_validation ${BLAS_LIBRARY})
target_link_libraries(test_deformation_validation ${CMAKE_THREAD_LIBS_INIT})
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_deformation_validation OpenMP::OpenMP_CXX)
endif()

# Test 9: test_filter_validation - Unit test for Ben's image filter
add_executable(test_filter_validation
    src/test_filter_validation.cpp
    ${CMAKE_SOURCE_DIR}/../src/image_processor.cpp
    ${CMAKE_SOURCE_DIR}/../src/utils.cpp
    ${CMAKE_SOURCE_DIR}/../src/dic_structures.cpp
)

# Link matio library
target_link_libraries(test_filter_validation ${MATIO_LIBRARY})

# Link HDF5 library
if(HDF5_FOUND)
    target_include_directories(test_filter_validation PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(test_filter_validation ${HDF5_C_LIBRARIES})
endif()

# Link OpenCV
target_link_libraries(test_filter_validation ${OpenCV_LIBS})

# Link Eigen3
if(TARGET Eigen3::Eigen)
    target_link_libraries(test_filter_validation Eigen3::Eigen)
endif()

# Link ncorr library
if(NCORR_LIBRARY)
    target_link_libraries(test_filter_validation ${NCORR_LIBRARY})
endif()

# Link other dependencies
if(FFTW_LIBRARY)
    target_link_libraries(test_filter_validation ${FFTW_LIBRARY})
endif()
target_link_libraries(test_filter_validation ${SUITESPARSE_LIBRARY1})
target_link_libraries(test_filter_validation ${SUITESPARSE_LIBRARY2})
target_link_libraries(test_filter_validation ${SUITESPARSE_LIBRARY3})
target_link_libraries(test_filter_validation ${SUITESPARSE_LIBRARY4})
target_link_libraries(test_filter_validation ${SUITESPARSE_LIBRARY5})
target_link_libraries(test_filter_validation ${LAPACK_LIBRARY})
target_link_libraries(test_filter_validation ${BLAS_LIBRARY})
target_link_libraries(test_filter_validation ${CMAKE_THREAD_LIBS_INIT})
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_filter_validation OpenMP::OpenMP_CXX)
endif()

# Test 10: test_unit_stitching - Unit tests for surface stitching functions
add_executable(test_unit_stitching
    src/test_unit_stitching.cpp
    ${CMAKE_SOURCE_DIR}/../src/surface_stitching.cpp
    ${CMAKE_SOURCE_DIR}/../src/dic_structures.cpp
)

# Link Eigen3
if(TARGET Eigen3::Eigen)
    target_link_libraries(test_unit_stitching Eigen3::Eigen)
endif()

# Link OpenCV (required by dic_structures)
target_link_libraries(test_unit_stitching ${OpenCV_LIBS})

# Link ncorr library
if(NCORR_LIBRARY)
    target_link_libraries(test_unit_stitching ${NCORR_LIBRARY})
endif()

# Link other dependencies (needed by ncorr / dic_structures)
if(FFTW_LIBRARY)
    target_link_libraries(test_unit_stitching ${FFTW_LIBRARY})
endif()
target_link_libraries(test_unit_stitching ${SUITESPARSE_LIBRARY1})
target_link_libraries(test_unit_stitching ${SUITESPARSE_LIBRARY2})
target_link_libraries(test_unit_stitching ${SUITESPARSE_LIBRARY3})
target_link_libraries(test_unit_stitching ${SUITESPARSE_LIBRARY4})
target_link_libraries(test_unit_stitching ${SUITESPARSE_LIBRARY5})
target_link_libraries(test_unit_stitching ${LAPACK_LIBRARY})
target_link_libraries(test_unit_stitching ${BLAS_LIBRARY})
target_link_libraries(test_unit_stitching ${CMAKE_THREAD_LIBS_INIT})
if(OpenMP_CXX_FOUND)
    target_link_libraries(test_unit_stitching OpenMP::OpenMP_CXX)
endif()

# Common project sources (all sources except main.cpp) for integration tests
# that need the full DicAnalysis pipeline
set(PROJECT_SOURCES
    ${CMAKE_SOURCE_DIR}/../src/config.cpp
    ${CMAKE_SOURCE_DIR}/../src/dic_analysis.cpp
    ${CMAKE_SOURCE_DIR}/../src/dic_structures.cpp
    ${CMAKE_SOURCE_DIR}/../src/utils.cpp
    ${CMAKE_SOURCE_DIR}/../src/image_processor.cpp
    ${CMAKE_SOURCE_DIR}/../src/roi_manager.cpp
    ${CMAKE_SOURCE_DIR}/../src/mat_writer.cpp
    ${CMAKE_SOURCE_DIR}/../src/io/mat/mat_ncorr_writer.cpp
    ${CMAKE_SOURCE_DIR}/../src/io/mat/mat_results_writer.cpp
    ${CMAKE_SOURCE_DIR}/../src/mat_reader.cpp
    ${CMAKE_SOURCE_DIR}/../src/step_d_workflow.cpp
    ${CMAKE_SOURCE_DIR}/../src/strain_computation.cpp
    ${CMAKE_SOURCE_DIR}/../src/delaunay_triangulation.cpp
    ${CMAKE_SOURCE_DIR}/../src/surface_stitching.cpp
    ${CMAKE_SOURCE_DIR}/../src/temporal_filter.cpp
    ${CMAKE_SOURCE_DIR}/../src/face_isotropy.cpp
    ${CMAKE_SOURCE_DIR}/../src/visualization.cpp
    ${CMAKE_SOURCE_DIR}/../src/data_serializer.cpp
    ${CMAKE_SOURCE_DIR}/../src/matlab_functions.cpp
    ${CMAKE_SOURCE_DIR}/../src/app/cli.cpp
    ${CMAKE_SOURCE_DIR}/../src/app/app_config.cpp
    ${CMAKE_SOURCE_DIR}/../src/io/project_paths.cpp
    ${CMAKE_SOURCE_DIR}/../src/io/mat/matio_helpers.cpp
    ${CMAKE_SOURCE_DIR}/../src/io/mat/mat_schema.cpp
    ${CMAKE_SOURCE_DIR}/../src/pipeline/trial_selector.cpp
    ${CMAKE_SOURCE_DIR}/../src/pipeline/checkpoint_policy.cpp
    ${CMAKE_SOURCE_DIR}/../src/pipeline/dic2d_frame_preparer.cpp
    ${CMAKE_SOURCE_DIR}/../src/pipeline/dic2d_matching_service.cpp
    ${CMAKE_SOURCE_DIR}/../src/pipeline/dic2d_ncorr_runner.cpp
    ${CMAKE_SOURCE_DIR}/../src/pipeline/dic2d_output_formatter.cpp
    ${CMAKE_SOURCE_DIR}/../src/pipeline/dic2d_tracking_service.cpp
    ${CMAKE_SOURCE_DIR}/../src/pipeline/step_d_runner.cpp
    ${CMAKE_SOURCE_DIR}/../src/pipeline/step_e_runner.cpp
    ${CMAKE_SOURCE_DIR}/../src/pipeline/step_f_runner.cpp
    ${CMAKE_SOURCE_DIR}/../src/pipeline/pipeline_runner.cpp
)

# Helper macro: link all project dependencies to a target
macro(link_all_project_deps TARGET_NAME)
    target_link_libraries(${TARGET_NAME} ${MATIO_LIBRARY})
    target_link_libraries(${TARGET_NAME} ${OpenCV_LIBS})
    if(TARGET Eigen3::Eigen)
        target_link_libraries(${TARGET_NAME} Eigen3::Eigen)
    endif()
    if(NCORR_LIBRARY)
        target_link_libraries(${TARGET_NAME} ${NCORR_LIBRARY})
    endif()
    if(FFTW_LIBRARY)
        target_link_libraries(${TARGET_NAME} ${FFTW_LIBRARY})
    endif()
    if(HDF5_FOUND)
        target_include_directories(${TARGET_NAME} PRIVATE ${HDF5_INCLUDE_DIRS})
        target_link_libraries(${TARGET_NAME} ${HDF5_C_LIBRARIES})
    endif()
    target_link_libraries(${TARGET_NAME} ${SUITESPARSE_LIBRARY1})
    target_link_libraries(${TARGET_NAME} ${SUITESPARSE_LIBRARY2})
    target_link_libraries(${TARGET_NAME} ${SUITESPARSE_LIBRARY3})
    target_link_libraries(${TARGET_NAME} ${SUITESPARSE_LIBRARY4})
    target_link_libraries(${TARGET_NAME} ${SUITESPARSE_LIBRARY5})
    target_link_libraries(${TARGET_NAME} ${LAPACK_LIBRARY})
    target_link_libraries(${TARGET_NAME} ${BLAS_LIBRARY})
    target_link_libraries(${TARGET_NAME} ${CMAKE_THREAD_LIBS_INIT})
    if(CGAL_FOUND)
        target_link_libraries(${TARGET_NAME} CGAL::CGAL)
    endif()
    if(OpenMP_CXX_FOUND)
        target_link_libraries(${TARGET_NAME} OpenMP::OpenMP_CXX)
    endif()
endmacro()

# Test 11: test_reconstruction_integration - calls DicAnalysis::runStepE() directly
add_executable(test_reconstruction_integration
    src/test_reconstruction_integration.cpp
    ${PROJECT_SOURCES}
)
link_all_project_deps(test_reconstruction_integration)

# Test 12: test_deformation_integration - calls DicAnalysis::runStepF() directly
add_executable(test_deformation_integration
    src/test_deformation_integration.cpp
    ${PROJECT_SOURCES}
)
link_all_project_deps(test_deformation_integration)

# Test 13: test_refactor_guards - coverage for new app/pipeline/io boundaries
add_executable(test_refactor_guards
    src/test_refactor_guards.cpp
    ${PROJECT_SOURCES}
)
link_all_project_deps(test_refactor_guards)

# Compiler flags
target_compile_options(test_ncorr_bin PRIVATE -Wall -Wextra -O2)
target_compile_options(test_ncorr_to_mat PRIVATE -Wall -Wextra -O2)
target_compile_options(test_mat_comparator PRIVATE -Wall -Wextra -O2)
target_compile_options(test_mat_writers PRIVATE -Wall -Wextra -O2)
target_compile_options(test_var_test2 PRIVATE -Wall -Wextra -O2)
target_compile_options(test_var_parity PRIVATE -Wall -Wextra -O2)
target_compile_options(test_pipeline_validation PRIVATE -Wall -Wextra -O2)
target_compile_options(test_deformation_validation PRIVATE -Wall -Wextra -O2)
target_compile_options(test_filter_validation PRIVATE -Wall -Wextra -O2)
target_compile_options(test_unit_stitching PRIVATE -Wall -Wextra -O2)
target_compile_options(test_reconstruction_integration PRIVATE -Wall -Wextra -O2)
target_compile_options(test_deformation_integration PRIVATE -Wall -Wextra -O2)
target_compile_options(test_refactor_guards PRIVATE -Wall -Wextra -O2)

# Print configuration
message(STATUS "CPPxDIC Tests Configuration:")
message(STATUS "  Output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "  OpenCV version: ${OpenCV_VERSION}")
