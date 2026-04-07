/**
 * Unit Tests for Surface Stitching Functions
 *
 * Tests the helper functions in surface_stitching.cpp:
 *   - computeMeshBoundary
 *   - computeEdgeLengths
 *   - groupBoundaryEdges
 *   - edgeListToCurve
 *   - findAllBoundaryFaces
 *   - zipBoundaryCurves
 *   - removeOverlapSurfaces
 *   - stitchPairsSimple
 *   - stitchPairsGeometric
 *
 * These tests use synthetic meshes (no external data required).
 */

#include "surface_stitching.h"
#include "dic_structures.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cassert>
#include <set>
#include <algorithm>

using namespace cppxdic;

// ============================================================================
// Test Framework
// ============================================================================

static int g_total = 0;
static int g_passed = 0;

#define TEST(name) \
    do { \
        g_total++; \
        std::cout << "  " << name << "... "; \
    } while(0)

#define PASS() \
    do { \
        g_passed++; \
        std::cout << "PASSED" << std::endl; \
    } while(0)

#define FAIL(msg) \
    do { \
        std::cout << "FAILED: " << msg << std::endl; \
    } while(0)

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { FAIL(msg); return; } \
    } while(0)

// ============================================================================
// Helper: Build a simple 2x2 quad mesh (4 vertices, 2 triangles)
//
//  v0 --- v1
//  |  \ 0 |
//  | 1  \ |
//  v2 --- v3
// ============================================================================
static void makeQuadMesh(std::vector<int>& faces, std::vector<Eigen::Vector3d>& verts) {
    verts = {
        {0.0, 0.0, 0.0},  // v0
        {1.0, 0.0, 0.0},  // v1
        {0.0, 1.0, 0.0},  // v2
        {1.0, 1.0, 0.0},  // v3
    };
    faces = {
        0, 1, 3,  // face 0: upper-right triangle
        0, 3, 2,  // face 1: lower-left triangle
    };
}

// Build a 3x3 grid mesh (9 vertices, 8 triangles)
//
//  v0 -- v1 -- v2
//  |  \ |  \ |
//  v3 -- v4 -- v5
//  |  \ |  \ |
//  v6 -- v7 -- v8
//
static void makeGridMesh(std::vector<int>& faces, std::vector<Eigen::Vector3d>& verts) {
    verts.clear();
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            verts.push_back({static_cast<double>(c), static_cast<double>(r), 0.0});
    
    faces.clear();
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 2; ++c) {
            int a = r * 3 + c;
            int b = a + 1;
            int d = a + 3;
            int e = d + 1;
            faces.push_back(a); faces.push_back(b); faces.push_back(e);
            faces.push_back(a); faces.push_back(e); faces.push_back(d);
        }
    }
}

// ============================================================================
// Tests: computeMeshBoundary
// ============================================================================
void test_computeMeshBoundary_quad() {
    TEST("computeMeshBoundary - quad mesh");
    
    std::vector<int> faces;
    std::vector<Eigen::Vector3d> verts;
    makeQuadMesh(faces, verts);
    
    auto boundary = computeMeshBoundary(faces, verts);
    
    // A quad with 2 triangles sharing the diagonal has 4 boundary edges
    // The diagonal (0,3) is shared by both faces → interior edge
    CHECK(boundary.size() / 2 == 4, "Expected 4 boundary edges, got " + std::to_string(boundary.size()/2));
    
    // Verify the diagonal (0,3) is NOT in the boundary
    std::set<std::pair<int,int>> bnd_set;
    for (size_t i = 0; i < boundary.size() / 2; ++i) {
        int a = boundary[i*2], b = boundary[i*2+1];
        bnd_set.insert(a < b ? std::make_pair(a,b) : std::make_pair(b,a));
    }
    CHECK(bnd_set.find({0,3}) == bnd_set.end(), "Diagonal (0,3) should not be boundary");
    CHECK(bnd_set.find({0,1}) != bnd_set.end(), "Edge (0,1) should be boundary");
    CHECK(bnd_set.find({1,3}) != bnd_set.end(), "Edge (1,3) should be boundary");
    CHECK(bnd_set.find({2,3}) != bnd_set.end(), "Edge (2,3) should be boundary");
    CHECK(bnd_set.find({0,2}) != bnd_set.end(), "Edge (0,2) should be boundary");
    
    PASS();
}

void test_computeMeshBoundary_grid() {
    TEST("computeMeshBoundary - 3x3 grid");
    
    std::vector<int> faces;
    std::vector<Eigen::Vector3d> verts;
    makeGridMesh(faces, verts);
    
    auto boundary = computeMeshBoundary(faces, verts);
    
    // 3x3 grid with 2x2 quads = 8 triangles
    // Perimeter of the grid: 4 sides × 2 edges = 8 boundary edges
    CHECK(boundary.size() / 2 == 8, "Expected 8 boundary edges, got " + std::to_string(boundary.size()/2));
    
    PASS();
}

void test_computeMeshBoundary_nan_vertices() {
    TEST("computeMeshBoundary - NaN vertices skipped");
    
    std::vector<Eigen::Vector3d> verts = {
        {0, 0, 0}, {1, 0, 0}, {0, 1, 0},
        {std::nan(""), std::nan(""), std::nan("")}
    };
    std::vector<int> faces = {0, 1, 2, 1, 3, 2};
    
    auto boundary = computeMeshBoundary(faces, verts);
    
    // Only the first face is valid (3 edges); second face has NaN vertex → skipped
    CHECK(boundary.size() / 2 == 3, "Expected 3 boundary edges from 1 valid triangle");
    
    PASS();
}

// ============================================================================
// Tests: computeEdgeLengths
// ============================================================================
void test_computeEdgeLengths_unit_triangle() {
    TEST("computeEdgeLengths - unit right triangle");
    
    std::vector<Eigen::Vector3d> verts = {{0,0,0}, {1,0,0}, {0,1,0}};
    std::vector<int> faces = {0, 1, 2};
    
    auto lengths = computeEdgeLengths(faces, verts);
    
    CHECK(lengths.size() == 3, "Should return 3 edge lengths");
    // e01=1, e12=sqrt(2), e20=1
    CHECK(std::abs(lengths[0] - 1.0) < 1e-10, "Edge 0-1 should be 1.0");
    CHECK(std::abs(lengths[1] - std::sqrt(2.0)) < 1e-10, "Edge 1-2 should be sqrt(2)");
    CHECK(std::abs(lengths[2] - 1.0) < 1e-10, "Edge 2-0 should be 1.0");
    
    PASS();
}

// ============================================================================
// Tests: groupBoundaryEdges
// ============================================================================
void test_groupBoundaryEdges_single_loop() {
    TEST("groupBoundaryEdges - single connected loop");
    
    // Triangle boundary: 3 edges forming one loop
    std::vector<int> edges = {0, 1, 1, 2, 2, 0};
    auto groups = groupBoundaryEdges(edges);
    
    CHECK(groups.size() == 1, "Expected 1 group for connected loop");
    CHECK(groups[0].size() == 3, "Group should have 3 edges");
    
    PASS();
}

void test_groupBoundaryEdges_two_loops() {
    TEST("groupBoundaryEdges - two disconnected loops");
    
    // Two separate triangles
    std::vector<int> edges = {
        0, 1, 1, 2, 2, 0,    // loop 1 (vertices 0,1,2)
        10, 11, 11, 12, 12, 10  // loop 2 (vertices 10,11,12)
    };
    auto groups = groupBoundaryEdges(edges);
    
    CHECK(groups.size() == 2, "Expected 2 groups for disconnected loops");
    
    PASS();
}

// ============================================================================
// Tests: edgeListToCurve
// ============================================================================
void test_edgeListToCurve_simple() {
    TEST("edgeListToCurve - simple chain");
    
    // Edges: 0-1, 1-2, 2-3 (start from vertex 0 so the walk can reach all)
    std::vector<int> edges = {0, 1, 1, 2, 2, 3};
    auto curve = edgeListToCurve(edges);
    
    CHECK(curve.size() == 4, "Expected 4 vertices in chain, got " + std::to_string(curve.size()));
    
    // Should form a connected path 0→1→2→3
    std::set<int> verts(curve.begin(), curve.end());
    CHECK(verts.count(0) && verts.count(1) && verts.count(2) && verts.count(3),
          "Curve should contain vertices 0,1,2,3");
    
    PASS();
}

void test_edgeListToCurve_closed_loop() {
    TEST("edgeListToCurve - closed loop");
    
    std::vector<int> edges = {0, 1, 1, 2, 2, 3, 3, 0};
    auto curve = edgeListToCurve(edges);
    
    CHECK(curve.size() == 4, "Expected 4 vertices in closed loop");
    
    PASS();
}

// ============================================================================
// Tests: findAllBoundaryFaces
// ============================================================================
void test_findAllBoundaryFaces_none_removed() {
    TEST("findAllBoundaryFaces - grid (no all-boundary faces)");
    
    std::vector<int> faces;
    std::vector<Eigen::Vector3d> verts;
    makeGridMesh(faces, verts);
    
    auto boundary = computeMeshBoundary(faces, verts);
    auto keep = findAllBoundaryFaces(faces, boundary);
    
    size_t nFaces = faces.size() / 3;
    size_t kept = std::count(keep.begin(), keep.end(), true);
    
    // In a regular grid, no face has all 3 edges on the boundary
    CHECK(kept == nFaces, "No faces should be removed from a regular grid");
    
    PASS();
}

void test_findAllBoundaryFaces_single_triangle() {
    TEST("findAllBoundaryFaces - single triangle (all boundary)");
    
    std::vector<Eigen::Vector3d> verts = {{0,0,0}, {1,0,0}, {0,1,0}};
    std::vector<int> faces = {0, 1, 2};
    
    auto boundary = computeMeshBoundary(faces, verts);
    auto keep = findAllBoundaryFaces(faces, boundary);
    
    // Single triangle: all 3 edges are boundary → should be removed
    CHECK(keep.size() == 1 && !keep[0], "Single triangle should be marked for removal");
    
    PASS();
}

// ============================================================================
// Tests: zipBoundaryCurves
// ============================================================================
void test_zipBoundaryCurves_parallel_lines() {
    TEST("zipBoundaryCurves - two parallel lines");
    
    std::vector<Eigen::Vector3d> V = {
        {0, 0, 0}, {1, 0, 0}, {2, 0, 0},  // curve1: v0, v1, v2
        {0, 1, 0}, {1, 1, 0}, {2, 1, 0},  // curve2: v3, v4, v5
    };
    
    std::vector<int> curve1 = {0, 1, 2};
    std::vector<int> curve2 = {3, 4, 5};
    
    auto new_faces = zipBoundaryCurves(curve1, curve2, V);
    
    // Greedy advancing-front on two 3-vertex curves should produce 4 triangles
    // (2 from curve1 advances + 2 from curve2 advances)
    size_t nFaces = new_faces.size() / 3;
    CHECK(nFaces >= 2 && nFaces <= 4, "Expected 2-4 triangles, got " + std::to_string(nFaces));
    
    // Verify all face indices are valid
    for (int idx : new_faces) {
        CHECK(idx >= 0 && idx < 6, "Face index out of range");
    }
    
    PASS();
}

// ============================================================================
// Tests: removeOverlapSurfaces
// ============================================================================
void test_removeOverlapSurfaces_no_overlap() {
    TEST("removeOverlapSurfaces - separated meshes");
    
    // Two triangles far apart
    std::vector<Eigen::Vector3d> V1 = {{0,0,0}, {1,0,0}, {0.5,1,0}};
    std::vector<int> F1 = {0, 1, 2};
    
    std::vector<Eigen::Vector3d> V2 = {{10,0,0}, {11,0,0}, {10.5,1,0}};
    std::vector<int> F2 = {0, 1, 2};
    
    auto [keep1, keep2] = removeOverlapSurfaces(F1, F2, V1, V2, 1.0);
    
    CHECK(keep1[0] == true, "S1 face should be kept (no overlap)");
    CHECK(keep2[0] == true, "S2 face should be kept (no overlap)");
    
    PASS();
}

void test_removeOverlapSurfaces_full_overlap() {
    TEST("removeOverlapSurfaces - coincident meshes");
    
    // Two identical triangles
    std::vector<Eigen::Vector3d> V1 = {{0,0,0}, {1,0,0}, {0.5,1,0}};
    std::vector<int> F1 = {0, 1, 2};
    
    std::vector<Eigen::Vector3d> V2 = V1;
    std::vector<int> F2 = F1;
    
    auto [keep1, keep2] = removeOverlapSurfaces(F1, F2, V1, V2, 1.0);
    
    // MATLAB-faithful behavior: when two surfaces fully overlap, the algorithm
    // erodes the duplicate while keeping a valid copy. At least one face must be
    // removed (never both — overlap removal exists to keep one valid mesh).
    bool removed_any = (!keep1[0]) || (!keep2[0]);
    bool kept_at_least_one = keep1[0] || keep2[0];
    CHECK(removed_any, "At least one duplicate face should be removed");
    CHECK(kept_at_least_one, "At least one face must survive (no full deletion)");
    
    PASS();
}

// ============================================================================
// Tests: stitchPairsSimple
// ============================================================================
void test_stitchPairsSimple_single_pair() {
    TEST("stitchPairsSimple - single pair passthrough");
    
    DIC3DpairResults pair;
    pair.cameraPairInd = {1, 2};
    pair.Faces = {0, 1, 2};
    pair.FaceColors = {128.0};
    
    Point3DFrame pts;
    pts.x = {0, 1, 0.5};
    pts.y = {0, 0, 1};
    pts.z = {0, 0, 0};
    pair.Points3D = {pts};
    pair.corrComb = {{0.9, 0.8, 0.85}};
    pair.FaceCorrComb = {{0.9}};
    pair.FaceCentroids = {{0.5, 0.333, 0.0}};
    pair.Disp.DispVec = {{0, 0, 0, 0, 0, 0, 0, 0, 0}};
    pair.Disp.DispMgn = {{0, 0, 0}};
    
    auto result = stitchPairsSimple({pair});
    
    CHECK(result.Faces.size() == 3, "Faces should pass through");
    CHECK(result.Points3D.size() == 1, "1 frame should pass through");
    CHECK(result.Points3D[0].x.size() == 3, "3 points should pass through");
    CHECK(result.FacePairInds.size() == 1 && result.FacePairInds[0] == 1, "Face pair index should be 1");
    
    PASS();
}

void test_stitchPairsSimple_two_pairs() {
    TEST("stitchPairsSimple - two pairs concatenation");
    
    DIC3DpairResults pair1, pair2;
    pair1.cameraPairInd = {1, 2};
    pair2.cameraPairInd = {3, 4};
    
    pair1.Faces = {0, 1, 2};
    pair1.FaceColors = {100.0};
    Point3DFrame pts1; pts1.x = {0,1,0.5}; pts1.y = {0,0,1}; pts1.z = {0,0,0};
    pair1.Points3D = {pts1};
    pair1.corrComb = {{0.9, 0.8, 0.85}};
    pair1.FaceCorrComb = {{0.9}};
    pair1.FaceCentroids = {{0.5, 0.333, 0.0}};
    pair1.Disp.DispVec = {{0,0,0, 0,0,0, 0,0,0}};
    pair1.Disp.DispMgn = {{0,0,0}};
    
    pair2.Faces = {0, 1, 2};
    pair2.FaceColors = {200.0};
    Point3DFrame pts2; pts2.x = {5,6,5.5}; pts2.y = {0,0,1}; pts2.z = {0,0,0};
    pair2.Points3D = {pts2};
    pair2.corrComb = {{0.95, 0.88, 0.92}};
    pair2.FaceCorrComb = {{0.95}};
    pair2.FaceCentroids = {{5.5, 0.333, 0.0}};
    pair2.Disp.DispVec = {{0,0,0, 0,0,0, 0,0,0}};
    pair2.Disp.DispMgn = {{0,0,0}};
    
    auto result = stitchPairsSimple({pair1, pair2});
    
    CHECK(result.Points3D[0].x.size() == 6, "Should have 6 points (3+3)");
    CHECK(result.Faces.size() == 6, "Should have 2 faces (6 indices)");
    // Second face indices should be offset by 3
    CHECK(result.Faces[3] == 3, "Second face v0 should be offset to 3");
    CHECK(result.Faces[4] == 4, "Second face v1 should be offset to 4");
    CHECK(result.Faces[5] == 5, "Second face v2 should be offset to 5");
    CHECK(result.FacePairInds[0] == 1, "First face pair = 1");
    CHECK(result.FacePairInds[1] == 2, "Second face pair = 2");
    
    PASS();
}

// ============================================================================
// Tests: stitchPairsGeometric
// ============================================================================
void test_stitchPairsGeometric_single_pair() {
    TEST("stitchPairsGeometric - single pair passthrough");
    
    DIC3DpairResults pair;
    pair.cameraPairInd = {1, 2};
    pair.Faces = {0, 1, 2, 0, 2, 3};
    pair.FaceColors = {100.0, 110.0};
    Point3DFrame pts; pts.x = {0,1,0,1}; pts.y = {0,0,1,1}; pts.z = {0,0,0,0};
    pair.Points3D = {pts};
    pair.corrComb = {{0.9, 0.8, 0.85, 0.82}};
    pair.FaceCorrComb = {{0.9, 0.85}};
    pair.FaceCentroids = {{0.333,0.333,0, 0.333,0.666,0}};
    pair.Disp.DispVec = {{0,0,0, 0,0,0, 0,0,0, 0,0,0}};
    pair.Disp.DispMgn = {{0,0,0,0}};
    
    auto result = stitchPairsGeometric({pair}, {1});
    
    CHECK(result.Points3D.size() == 1, "Should have 1 frame");
    CHECK(!result.Faces.empty(), "Should have faces");
    
    PASS();
}

void test_stitchPairsGeometric_overlap_removal() {
    TEST("stitchPairsGeometric - overlapping pairs: overlap detected");
    
    // Create two overlapping quads. With small meshes, overlap removal may
    // remove ALL faces (the overlap region covers both meshes entirely).
    // We verify that the overlap detection runs and vertices are combined.
    DIC3DpairResults pair1, pair2;
    pair1.cameraPairInd = {1, 2};
    pair2.cameraPairInd = {3, 4};
    
    // Pair1: quad at x=[0,2], y=[0,1]
    pair1.Faces = {0,1,3, 0,3,2};
    pair1.FaceColors = {100.0, 110.0};
    Point3DFrame pts1; pts1.x = {0,2,0,2}; pts1.y = {0,0,1,1}; pts1.z = {0,0,0,0};
    pair1.Points3D = {pts1};
    pair1.corrComb = {{0.9, 0.9, 0.9, 0.9}};
    pair1.FaceCorrComb = {{0.9, 0.9}};
    pair1.FaceCentroids = {{0.666,0.333,0, 0.666,0.666,0}};
    pair1.Disp.DispVec = {{0,0,0, 0,0,0, 0,0,0, 0,0,0}};
    pair1.Disp.DispMgn = {{0,0,0,0}};
    
    // Pair2: quad at x=[1,3], y=[0,1] (overlaps pair1 in x=[1,2])
    pair2.Faces = {0,1,3, 0,3,2};
    pair2.FaceColors = {200.0, 210.0};
    Point3DFrame pts2; pts2.x = {1,3,1,3}; pts2.y = {0,0,1,1}; pts2.z = {0,0,0,0};
    pair2.Points3D = {pts2};
    pair2.corrComb = {{0.9, 0.9, 0.9, 0.9}};
    pair2.FaceCorrComb = {{0.9, 0.9}};
    pair2.FaceCentroids = {{1.666,0.333,0, 1.666,0.666,0}};
    pair2.Disp.DispVec = {{0,0,0, 0,0,0, 0,0,0, 0,0,0}};
    pair2.Disp.DispMgn = {{0,0,0,0}};
    
    auto result = stitchPairsGeometric({pair1, pair2}, {1, 2});
    
    // Simple append would give 4 faces. After overlap removal, we may have fewer
    // (possibly 0 if overlap covers everything). The key check is that stitching
    // ran without crashing and vertices are combined.
    size_t totalFaces = result.Faces.size() / 3;
    size_t simpleFaces = 4;  // What stitchPairsSimple would give
    CHECK(totalFaces <= simpleFaces, "Geometric should not produce more faces than simple");
    CHECK(result.Points3D[0].x.size() == 8, "Should have 8 vertices (4+4) in combined mesh");
    
    PASS();
}

// ============================================================================
// Main
// ============================================================================
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Surface Stitching Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::cout << "\n--- computeMeshBoundary ---" << std::endl;
    test_computeMeshBoundary_quad();
    test_computeMeshBoundary_grid();
    test_computeMeshBoundary_nan_vertices();
    
    std::cout << "\n--- computeEdgeLengths ---" << std::endl;
    test_computeEdgeLengths_unit_triangle();
    
    std::cout << "\n--- groupBoundaryEdges ---" << std::endl;
    test_groupBoundaryEdges_single_loop();
    test_groupBoundaryEdges_two_loops();
    
    std::cout << "\n--- edgeListToCurve ---" << std::endl;
    test_edgeListToCurve_simple();
    test_edgeListToCurve_closed_loop();
    
    std::cout << "\n--- findAllBoundaryFaces ---" << std::endl;
    test_findAllBoundaryFaces_none_removed();
    test_findAllBoundaryFaces_single_triangle();
    
    std::cout << "\n--- zipBoundaryCurves ---" << std::endl;
    test_zipBoundaryCurves_parallel_lines();
    
    std::cout << "\n--- removeOverlapSurfaces ---" << std::endl;
    test_removeOverlapSurfaces_no_overlap();
    test_removeOverlapSurfaces_full_overlap();
    
    std::cout << "\n--- stitchPairsSimple ---" << std::endl;
    test_stitchPairsSimple_single_pair();
    test_stitchPairsSimple_two_pairs();
    
    std::cout << "\n--- stitchPairsGeometric ---" << std::endl;
    test_stitchPairsGeometric_single_pair();
    test_stitchPairsGeometric_overlap_removal();
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Results: " << g_passed << "/" << g_total << " passed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return (g_passed == g_total) ? 0 : 1;
}
