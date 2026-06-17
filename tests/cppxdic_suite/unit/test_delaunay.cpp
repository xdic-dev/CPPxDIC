/**
 * @file test_delaunay.cpp
 * @brief Unit tests for DelaunayTriangulation (include/delaunay_triangulation.h).
 *
 * Tests the pure-geometry helpers on small known inputs:
 *  - compute: a unit square + centre triangulates into 4 triangles (12 face indices).
 *  - computeEdgeLengths: correct Euclidean lengths for a fixed right triangle.
 *  - filterByEdgeLength: drops triangles whose max edge exceeds a threshold.
 *  - flipOrientation: reverses winding (swaps 2nd/3rd vertex per face).
 */

#include "../framework/test_harness.h"
#include "delaunay_triangulation.h"

#include <cmath>

using cppxdic::DelaunayTriangulation;

TEST(delaunay, compute_square_plus_centre) {
    std::vector<cv::Point2f> pts = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}, {0.5f, 0.5f}};
    auto faces = DelaunayTriangulation::compute(pts);
    // 5 points (4 corners + centre) -> 4 triangles -> 12 flat indices.
    CHECK_EQ(faces.size(), static_cast<size_t>(12));
    // All indices in range.
    for (int idx : faces) {
        CHECK_TRUE(idx >= 0 && idx < static_cast<int>(pts.size()));
    }
}

TEST(delaunay, edge_lengths_right_triangle) {
    // 3-4-5 right triangle.
    std::vector<cv::Point2f> v = {{0.f, 0.f}, {3.f, 0.f}, {0.f, 4.f}};
    std::vector<int> faces = {0, 1, 2};
    auto lens = DelaunayTriangulation::computeEdgeLengths(faces, v);
    CHECK_EQ(lens.size(), static_cast<size_t>(3));
    // Edges (unordered): 3, 4, 5.
    double sum = 0;
    double maxlen = 0;
    for (double l : lens) {
        sum += l;
        if (l > maxlen) maxlen = l;
    }
    CHECK_NEAR(sum, 12.0, 1e-4);
    CHECK_NEAR(maxlen, 5.0, 1e-4);
}

TEST(delaunay, filter_by_edge_length) {
    std::vector<cv::Point2f> v = {
        {0.f, 0.f}, {1.f, 0.f},  {0.f, 1.f},  // small triangle, max edge ~1.41
        {0.f, 0.f}, {10.f, 0.f}, {0.f, 1.f}}; // big triangle, max edge ~10.05
    std::vector<int> faces = {0, 1, 2, 3, 4, 5};
    auto filtered = DelaunayTriangulation::filterByEdgeLength(faces, v, 5.0);
    // Only the small triangle survives.
    CHECK_EQ(filtered.size(), static_cast<size_t>(3));
    if (filtered.size() == 3) {
        CHECK_EQ(filtered[0], 0);
        CHECK_EQ(filtered[1], 1);
        CHECK_EQ(filtered[2], 2);
    }
}

TEST(delaunay, flip_orientation) {
    std::vector<int> faces = {0, 1, 2, 3, 4, 5};
    auto flipped = DelaunayTriangulation::flipOrientation(faces);
    CHECK_EQ(flipped.size(), static_cast<size_t>(6));
    // MATLAB F(:, [1 3 2]): keep first vertex, swap 2nd and 3rd.
    if (flipped.size() == 6) {
        CHECK_EQ(flipped[0], 0);
        CHECK_EQ(flipped[1], 2);
        CHECK_EQ(flipped[2], 1);
        CHECK_EQ(flipped[3], 3);
        CHECK_EQ(flipped[4], 5);
        CHECK_EQ(flipped[5], 4);
    }
}

TEST_MAIN()
