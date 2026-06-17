# ----------------------------------------------------------------------------
# xdic_stepsABC: standalone tool for MASK+Seed setup (GUI / headless) and StepC
# stereovision DLT calibration. Additive and OFF by default (BUILD_XDIC_STEPSABC).
#
# GUI support is optional via XDIC_STEPSABC_GUI (default ON). When OFF, the
# OpenCV highgui drawing code is #ifdef-guarded out so the binary builds and
# links on a headless cluster node with no display backend.
# ----------------------------------------------------------------------------

option(XDIC_STEPSABC_GUI "Build xdic_stepsABC with interactive OpenCV highgui support" ON)

add_executable(xdic_stepsABC
    apps/xdic_stepsABC/main.cpp
    src/stepsABC/mask_seed_setup.cpp
    src/stepsABC/stereo_calibration.cpp
    # Reuse existing pipeline math/util code rather than duplicating it:
    #   - Utils::DLT11Calibration (DLT 11-parameter fit) lives in src/utils.cpp
    #   - Config is required by utils.cpp
    src/utils.cpp
    src/config.cpp
)

target_include_directories(xdic_stepsABC PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Tools/CppNCorr/include
)

if(XDIC_STEPSABC_GUI)
    target_compile_definitions(xdic_stepsABC PRIVATE XDIC_STEPSABC_GUI=1)
    message(STATUS "xdic_stepsABC: GUI support ENABLED (XDIC_STEPSABC_GUI=ON)")
else()
    message(STATUS "xdic_stepsABC: GUI support DISABLED (headless build)")
endif()

# Link the same library set used by cppxdic (variables defined in the root
# CMakeLists). highgui is only strictly needed for the GUI build, but linking it
# unconditionally is harmless and keeps the dependency list identical to cppxdic.
target_link_libraries(xdic_stepsABC
    ncorr
    ${OpenCV_LIBS}
    ${OPENCV_LIBRARY1} ${OPENCV_LIBRARY2} ${OPENCV_LIBRARY3} ${OPENCV_LIBRARY4} ${OPENCV_LIBRARY5}
    ${FFTW_LIBRARY}
    ${MATIO_LIBRARY}
    ${SUITESPARSE_LIBRARY1} ${SUITESPARSE_LIBRARY2} ${SUITESPARSE_LIBRARY3}
    ${SUITESPARSE_LIBRARY4} ${SUITESPARSE_LIBRARY5}
    ${LAPACK_LIBRARY} ${BLAS_LIBRARY}
    ${CMAKE_THREAD_LIBS_INIT}
)

if(MATIO_INCLUDE_DIR)
    target_include_directories(xdic_stepsABC PRIVATE ${MATIO_INCLUDE_DIR})
endif()
if(HDF5_FOUND)
    target_include_directories(xdic_stepsABC PRIVATE ${HDF5_INCLUDE_DIRS})
    target_link_libraries(xdic_stepsABC ${HDF5_C_LIBRARIES})
endif()
if(FFTW_INCLUDE_DIR)
    target_include_directories(xdic_stepsABC PRIVATE ${FFTW_INCLUDE_DIR})
endif()
if(CGAL_FOUND)
    target_link_libraries(xdic_stepsABC CGAL::CGAL)
endif()
if(Eigen3_FOUND)
    if(TARGET Eigen3::Eigen)
        target_link_libraries(xdic_stepsABC Eigen3::Eigen)
    endif()
    if(DEFINED EIGEN3_INCLUDE_DIRS)
        target_include_directories(xdic_stepsABC PRIVATE ${EIGEN3_INCLUDE_DIRS})
    elseif(DEFINED EIGEN3_INCLUDE_DIR)
        target_include_directories(xdic_stepsABC PRIVATE ${EIGEN3_INCLUDE_DIR})
    endif()
endif()
if(nlohmann_json_FOUND)
    target_link_libraries(xdic_stepsABC nlohmann_json::nlohmann_json)
endif()
if(OpenMP_CXX_FOUND)
    target_link_libraries(xdic_stepsABC OpenMP::OpenMP_CXX)
endif()

target_compile_options(xdic_stepsABC PRIVATE -Wall -Wextra -O2)

install(TARGETS xdic_stepsABC DESTINATION bin)
