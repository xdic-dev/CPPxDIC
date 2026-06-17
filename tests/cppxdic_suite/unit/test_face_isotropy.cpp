/**
 * @file test_face_isotropy.cpp
 * @brief Unit tests for face isotropy index (include/face_isotropy.h).
 *
 *  - equilateral triangle -> isotropy index near its maximum (1.0).
 *  - degenerate sliver triangle -> low index (near 0).
 *  - computeFaceIsotropyIndex returns one value per face, all in the valid [0,1] range.
 */

#include "../framework/test_harness.h"
#include "face_isotropy.h"

#include <cmath>
#include <vector>

using cppxdic::computeFaceIsotropyIndex;
using cppxdic::computeSingleFaceIsotropyIndex;
using Eigen::Vector3d;

TEST(face_isotropy, equilateral_is_maximal) {
    // Equilateral triangle in the XY plane.
    Vector3d a(0.0, 0.0, 0.0);
    Vector3d b(1.0, 0.0, 0.0);
    Vector3d c(0.5, std::sqrt(3.0) / 2.0, 0.0);
    double iso = computeSingleFaceIsotropyIndex(a, b, c);
    CHECK_NEAR(iso, 1.0, 1e-3);
}

TEST(face_isotropy, sliver_is_low) {
    // Near-degenerate sliver: vertices almost collinear.
    Vector3d a(0.0, 0.0, 0.0);
    Vector3d b(1.0, 0.0, 0.0);
    Vector3d c(2.0, 1e-3, 0.0);
    double iso = computeSingleFaceIsotropyIndex(a, b, c);
    CHECK_TRUE(iso >= 0.0);
    CHECK_TRUE(iso < 0.2);
}

TEST(face_isotropy, per_face_values_in_range) {
    std::vector<Vector3d> verts = {{0.0, 0.0, 0.0},
                                   {1.0, 0.0, 0.0},
                                   {0.5, std::sqrt(3.0) / 2.0, 0.0}, // equilateral
                                   {2.0, 1e-3, 0.0}};                // sliver partner
    std::vector<int> faces = {0, 1, 2, 0, 1, 3};
    auto idx = computeFaceIsotropyIndex(faces, verts);
    CHECK_EQ(idx.size(), static_cast<size_t>(2));
    for (double v : idx) {
        CHECK_TRUE(v >= 0.0 && v <= 1.0 + 1e-9);
    }
    if (idx.size() == 2) {
        CHECK_TRUE(idx[0] > idx[1]); // equilateral more isotropic than sliver
    }
}

TEST_MAIN()
