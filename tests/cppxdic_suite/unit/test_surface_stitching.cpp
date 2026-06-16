/**
 * @file test_surface_stitching.cpp
 * @brief Unit tests for surface-stitching math (include/surface_stitching.h).
 *
 *  - computeEdgeLengths (3D): correct Euclidean lengths for fixed 3D vertices.
 *  - computeMeshBoundary: a two-triangle quad has exactly 4 boundary edges (no interior edge).
 *  - stitchPairsSimple: concatenation invariants - combined vertex/face counts equal the sum
 *    of inputs, and re-offset face indices stay valid.
 */

#include "../framework/test_harness.h"
#include "surface_stitching.h"

#include <Eigen/Dense>
#include <cmath>
#include <vector>

using Eigen::Vector3d;
using namespace cppxdic;

namespace {
/// Build a trivial single-frame DIC3DpairResults with the given vertices and faces.
DIC3DpairResults make_pair(const std::vector<Vector3d>& verts,
                           const std::vector<int>& faces) {
    DIC3DpairResults p;
    p.cameraPairInd = {1, 2};
    p.Faces = faces;
    p.FaceColors.assign(faces.size() / 3, 0.0);
    Points3D pts;
    for (const auto& v : verts) {
        pts.x.push_back(v.x());
        pts.y.push_back(v.y());
        pts.z.push_back(v.z());
    }
    p.Points3D.push_back(pts); // single frame
    p.corrComb.push_back(std::vector<double>(verts.size(), 0.0));
    p.FaceCorrComb.push_back(std::vector<double>(faces.size() / 3, 0.0));
    return p;
}
} // namespace

TEST(surface_stitching, edge_lengths_3d) {
    std::vector<Vector3d> v = {{0, 0, 0}, {3, 0, 0}, {0, 0, 4}};
    std::vector<int> faces = {0, 1, 2};
    auto lens = computeEdgeLengths(faces, v);
    CHECK_EQ(lens.size(), static_cast<size_t>(3));
    double sum = 0, maxlen = 0;
    for (double l : lens) {
        sum += l;
        maxlen = std::max(maxlen, l);
    }
    CHECK_NEAR(sum, 12.0, 1e-4); // 3 + 4 + 5
    CHECK_NEAR(maxlen, 5.0, 1e-4);
}

TEST(surface_stitching, mesh_boundary_of_quad) {
    // Two triangles sharing the diagonal (0-2) form a unit quad.
    std::vector<Vector3d> v = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    std::vector<int> faces = {0, 1, 2, 0, 2, 3};
    auto boundary = computeMeshBoundary(faces, v);
    // 4 boundary edges -> 8 flat indices; the shared diagonal (0-2) is interior, excluded.
    CHECK_EQ(boundary.size(), static_cast<size_t>(8));
    // Diagonal edge {0,2} must not appear as a boundary edge.
    bool has_diagonal = false;
    for (size_t i = 0; i + 1 < boundary.size(); i += 2) {
        int a = boundary[i], b = boundary[i + 1];
        if ((a == 0 && b == 2) || (a == 2 && b == 0)) has_diagonal = true;
    }
    CHECK_FALSE(has_diagonal);
}

TEST(surface_stitching, stitch_pairs_simple_concatenation) {
    // Pair A: 3 verts, 1 face. Pair B: 3 verts, 1 face.
    auto A = make_pair({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2});
    auto B = make_pair({{5, 0, 0}, {6, 0, 0}, {5, 1, 0}}, {0, 1, 2});

    std::vector<DIC3DpairResults> pairs = {A, B};
    DIC3Dcombined combined = stitchPairsSimple(pairs);

    REQUIRE_TRUE(!combined.Points3D.empty());
    // Vertex count = sum of inputs.
    CHECK_EQ(combined.Points3D[0].x.size(), static_cast<size_t>(6));
    // Face count = sum of inputs.
    CHECK_EQ(combined.Faces.size(), static_cast<size_t>(6));
    // Second pair's faces re-offset by the first pair's vertex count (3).
    CHECK_EQ(combined.Faces[3], 3);
    CHECK_EQ(combined.Faces[4], 4);
    CHECK_EQ(combined.Faces[5], 5);
    // All face indices valid against combined vertex set.
    int nVerts = static_cast<int>(combined.Points3D[0].x.size());
    for (int idx : combined.Faces) {
        CHECK_TRUE(idx >= 0 && idx < nVerts);
    }
    // Pair membership tagging.
    CHECK_EQ(combined.FacePairInds.size(), static_cast<size_t>(2));
    if (combined.FacePairInds.size() == 2) {
        CHECK_EQ(combined.FacePairInds[0], 1);
        CHECK_EQ(combined.FacePairInds[1], 2);
    }
}

TEST_MAIN()
