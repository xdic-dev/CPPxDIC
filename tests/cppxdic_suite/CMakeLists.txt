# ============================================================================
# CPPxDIC test suite (Section 5b)
#
# Uses a tiny in-tree assertion harness (framework/test_harness.h) + CTest.
# No GoogleTest / Catch2 -> zero new dependencies.
#
# This file is only added from the root CMakeLists when BUILD_TESTING is ON *and*
# CPPxDIC is the top-level project, so it never interferes with normal builds or with
# consumers that add CPPxDIC as a subdirectory. It is kept under tests/cppxdic_suite/ to
# stay clearly separate from the pre-existing tests/ infrastructure.
#
# Strategy: a static library `cppxdic_core` is built from the production source files the
# unit tests exercise, reusing the exact same third-party dependencies as the main
# `cppxdic` target (already discovered in the root CMakeLists). Each test is a small
# executable linking that library, registered with add_test() so `ctest` drives them.
# ============================================================================

set(SUITE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# ----------------------------------------------------------------------------
# Core library under test (subset of production sources needed by the tests).
# ----------------------------------------------------------------------------
add_library(cppxdic_core STATIC
    ${CMAKE_SOURCE_DIR}/src/config.cpp
    ${CMAKE_SOURCE_DIR}/src/dic_structures.cpp
    ${CMAKE_SOURCE_DIR}/src/delaunay_triangulation.cpp
    ${CMAKE_SOURCE_DIR}/src/surface_stitching.cpp
    ${CMAKE_SOURCE_DIR}/src/temporal_filter.cpp
    ${CMAKE_SOURCE_DIR}/src/face_isotropy.cpp
    ${CMAKE_SOURCE_DIR}/src/input/image_folder_reader.cpp
)

target_include_directories(cppxdic_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Tools/CppNCorr/include
)

# Reuse the same third-party deps the main target links (found in the root CMakeLists by
# the time this file is processed via add_subdirectory()).
target_link_libraries(cppxdic_core PUBLIC ${OpenCV_LIBS})
if(TARGET CGAL::CGAL)
    target_link_libraries(cppxdic_core PUBLIC CGAL::CGAL)
endif()
if(TARGET Eigen3::Eigen)
    target_link_libraries(cppxdic_core PUBLIC Eigen3::Eigen)
endif()
if(DEFINED EIGEN3_INCLUDE_DIRS)
    target_include_directories(cppxdic_core PUBLIC ${EIGEN3_INCLUDE_DIRS})
elseif(DEFINED EIGEN3_INCLUDE_DIR)
    target_include_directories(cppxdic_core PUBLIC ${EIGEN3_INCLUDE_DIR})
endif()
if(MATIO_LIBRARY)
    target_link_libraries(cppxdic_core PUBLIC ${MATIO_LIBRARY})
endif()
if(MATIO_INCLUDE_DIR)
    target_include_directories(cppxdic_core PUBLIC ${MATIO_INCLUDE_DIR})
endif()
if(HDF5_FOUND)
    target_include_directories(cppxdic_core PUBLIC ${HDF5_INCLUDE_DIRS})
    target_link_libraries(cppxdic_core PUBLIC ${HDF5_C_LIBRARIES})
endif()
if(FFTW_LIBRARY)
    target_link_libraries(cppxdic_core PUBLIC ${FFTW_LIBRARY})
endif()
if(FFTW_INCLUDE_DIR)
    target_include_directories(cppxdic_core PUBLIC ${FFTW_INCLUDE_DIR})
endif()
if(OpenMP_CXX_FOUND)
    target_link_libraries(cppxdic_core PUBLIC OpenMP::OpenMP_CXX)
endif()
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(cppxdic_core PUBLIC nlohmann_json::nlohmann_json)
elseif(NLOHMANN_JSON_INCLUDE_DIRS)
    target_include_directories(cppxdic_core PUBLIC ${NLOHMANN_JSON_INCLUDE_DIRS})
endif()
# SuiteSparse / LAPACK / BLAS (pulled in transitively by the math sources).
target_link_libraries(cppxdic_core PUBLIC
    ${SUITESPARSE_LIBRARY1} ${SUITESPARSE_LIBRARY2} ${SUITESPARSE_LIBRARY3}
    ${SUITESPARSE_LIBRARY4} ${SUITESPARSE_LIBRARY5}
    ${LAPACK_LIBRARY} ${BLAS_LIBRARY} ${CMAKE_THREAD_LIBS_INIT})

# Default xDIC mode for tests = camerapairs (matches the production default).
target_compile_definitions(cppxdic_core PUBLIC XDIC_MODE_CAMERAPAIRS)
target_compile_options(cppxdic_core PRIVATE -O2)

# ----------------------------------------------------------------------------
# Helper to register a test executable + CTest case.
# ----------------------------------------------------------------------------
function(cppxdic_add_test test_name source)
    add_executable(${test_name} ${SUITE_DIR}/${source})
    target_link_libraries(${test_name} PRIVATE cppxdic_core)
    target_include_directories(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

# Path to the lightweight 2D fixture (ohtcfrp) passed to integration / e2e tests.
set(OHTCFRP_DIR "${CMAKE_SOURCE_DIR}/Tools/CppNCorr/test/examples/ohtcfrp/images")

# ----------------------------------------------------------------------------
# Unit tests (all runnable / pure functions).
# ----------------------------------------------------------------------------
cppxdic_add_test(test_config              unit/test_config.cpp)
cppxdic_add_test(test_xdic_mode           unit/test_xdic_mode.cpp)
cppxdic_add_test(test_image_folder_reader unit/test_image_folder_reader.cpp)
cppxdic_add_test(test_delaunay            unit/test_delaunay.cpp)
cppxdic_add_test(test_temporal_filter     unit/test_temporal_filter.cpp)
cppxdic_add_test(test_face_isotropy       unit/test_face_isotropy.cpp)
cppxdic_add_test(test_surface_stitching   unit/test_surface_stitching.cpp)

# ----------------------------------------------------------------------------
# Integration tests.
#   IT-3 (2D DIC on ohtcfrp) is runnable today: it drives the image-folder reader against
#   the real fixture and asserts frame discovery / ordering invariants. The stereo-only
#   integration tests (IT-1,2,4,5,6,7) are registered as SKIPPED placeholders so the gaps
#   stay explicit until a stereo fixture is provided.
# ----------------------------------------------------------------------------
cppxdic_add_test(test_it3_ohtcfrp_fixture integration/test_it3_ohtcfrp_fixture.cpp)
target_compile_definitions(test_it3_ohtcfrp_fixture PRIVATE OHTCFRP_DIR="${OHTCFRP_DIR}")

cppxdic_add_test(test_integration_skipped integration/test_integration_skipped.cpp)

# ----------------------------------------------------------------------------
# End-to-end tests.
#   E2E-1 (2D path on ohtcfrp) asserts the captured golden baseline with tolerance bands.
#   E2E-2 (full stereo) is a SKIPPED placeholder (no in-repo stereo dataset).
# ----------------------------------------------------------------------------
cppxdic_add_test(test_e2e_ohtcfrp e2e/test_e2e_ohtcfrp.cpp)
target_compile_definitions(test_e2e_ohtcfrp PRIVATE OHTCFRP_DIR="${OHTCFRP_DIR}")
