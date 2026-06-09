/**
 * Surface Stitching Implementation for Multi-Pair 3D Reconstruction
 * Based on Matlab's DIC3DsurfaceStitch.m
 */

#include "dic_structures.h"
#include "surface_stitching.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>
#include <map>
#include <set>
#include <unordered_map>
#include <Eigen/Dense>

namespace cppxdic {

/**
 * Simple append-based stitching (no geometric overlap removal)
 * Equivalent to Matlab lines 450-479 (when pairIndList is empty)
 */
DIC3Dcombined stitchPairsSimple(const std::vector<DIC3DpairResults>& all_pairs) {
    if (all_pairs.empty()) {
        return DIC3Dcombined();
    }
    
    if (all_pairs.size() == 1) {
        // Single pair - convert to DIC3Dcombined with pair indices set
        DIC3Dcombined result;
        const auto& pair = all_pairs[0];
        
        result.Points3D = pair.Points3D;
        result.Faces = pair.Faces;
        result.FaceColors = pair.FaceColors;
        result.corrComb = pair.corrComb;
        result.FaceCorrComb = pair.FaceCorrComb;
        result.FaceCentroids = pair.FaceCentroids;
        result.Disp = pair.Disp;
        
        size_t nFaces = result.Faces.size() / 3;
        size_t nPoints = result.Points3D.empty() ? 0 : result.Points3D[0].x.size();
        result.FacePairInds.assign(nFaces, 1);
        result.PointPairInds.assign(nPoints, 1);
        
        // Set pairIndices from cameraPairInd
        if (pair.cameraPairInd.size() >= 2) {
            result.pairIndices = pair.cameraPairInd;
        }
        
        return result;
    }
    
    // Multi-pair: append all pairs together
    DIC3Dcombined stitched;
    size_t nFrames = all_pairs[0].Points3D.size();
    
    // Initialize with first pair
    const auto& first_pair = all_pairs[0];
    stitched.Points3D = first_pair.Points3D;
    stitched.Faces = first_pair.Faces;
    stitched.FaceColors = first_pair.FaceColors;
    stitched.corrComb = first_pair.corrComb;
    stitched.FaceCorrComb = first_pair.FaceCorrComb;
    stitched.FaceCentroids = first_pair.FaceCentroids;
    stitched.Disp = first_pair.Disp;
    
    size_t nFaces = stitched.Faces.size() / 3;
    size_t nPoints = stitched.Points3D[0].x.size();
    stitched.FacePairInds.assign(nFaces, 1);  // Pair 1
    stitched.PointPairInds.assign(nPoints, 1);
    
    // Initialize pairIndices matrix (nPairs x 2)
    stitched.pairIndices.resize(all_pairs.size() * 2);
    if (first_pair.cameraPairInd.size() >= 2) {
        stitched.pairIndices[0] = first_pair.cameraPairInd[0];
        stitched.pairIndices[1] = first_pair.cameraPairInd[1];
    }
    
    std::cout << "  Pair 1: " << nPoints << " points, " << nFaces << " faces" << std::endl;
    
    // Append subsequent pairs
    for (size_t ipair = 1; ipair < all_pairs.size(); ++ipair) {
        const auto& pair = all_pairs[ipair];
        size_t vertex_offset = stitched.Points3D[0].x.size();
        
        // Append faces with adjusted indices
        size_t pair_nFaces = pair.Faces.size() / 3;
        for (size_t i = 0; i < pair.Faces.size(); ++i) {
            stitched.Faces.push_back(pair.Faces[i] + static_cast<int>(vertex_offset));
        }
        
        // Append face colors
        stitched.FaceColors.insert(
            stitched.FaceColors.end(),
            pair.FaceColors.begin(),
            pair.FaceColors.end()
        );
        
        // Append face pair indices (1-indexed)
        stitched.FacePairInds.insert(
            stitched.FacePairInds.end(),
            pair_nFaces,
            static_cast<int>(ipair + 1)
        );
        
        // Append points for all frames
        size_t pair_nPoints = pair.Points3D[0].x.size();
        for (size_t frame = 0; frame < nFrames; ++frame) {
            // Append Points3D
            stitched.Points3D[frame].x.insert(
                stitched.Points3D[frame].x.end(),
                pair.Points3D[frame].x.begin(),
                pair.Points3D[frame].x.end()
            );
            stitched.Points3D[frame].y.insert(
                stitched.Points3D[frame].y.end(),
                pair.Points3D[frame].y.begin(),
                pair.Points3D[frame].y.end()
            );
            stitched.Points3D[frame].z.insert(
                stitched.Points3D[frame].z.end(),
                pair.Points3D[frame].z.begin(),
                pair.Points3D[frame].z.end()
            );
            
            // Append correlation
            if (frame < pair.corrComb.size()) {
                stitched.corrComb[frame].insert(
                    stitched.corrComb[frame].end(),
                    pair.corrComb[frame].begin(),
                    pair.corrComb[frame].end()
                );
            }
            
            // Append face correlation
            if (frame < pair.FaceCorrComb.size()) {
                stitched.FaceCorrComb[frame].insert(
                    stitched.FaceCorrComb[frame].end(),
                    pair.FaceCorrComb[frame].begin(),
                    pair.FaceCorrComb[frame].end()
                );
            }
            
            // Append face centroids (3 values per face)
            if (frame < pair.FaceCentroids.size()) {
                stitched.FaceCentroids[frame].insert(
                    stitched.FaceCentroids[frame].end(),
                    pair.FaceCentroids[frame].begin(),
                    pair.FaceCentroids[frame].end()
                );
            }
        }
        
        // Append displacement data
        for (size_t frame = 0; frame < nFrames; ++frame) {
            if (frame < pair.Disp.DispVec.size()) {
                stitched.Disp.DispVec[frame].insert(
                    stitched.Disp.DispVec[frame].end(),
                    pair.Disp.DispVec[frame].begin(),
                    pair.Disp.DispVec[frame].end()
                );
            }
            if (frame < pair.Disp.DispMgn.size()) {
                stitched.Disp.DispMgn[frame].insert(
                    stitched.Disp.DispMgn[frame].end(),
                    pair.Disp.DispMgn[frame].begin(),
                    pair.Disp.DispMgn[frame].end()
                );
            }
        }
        
        // Append point pair indices
        stitched.PointPairInds.insert(
            stitched.PointPairInds.end(),
            pair_nPoints,
            static_cast<int>(ipair + 1)
        );
        
        // Update pairIndices matrix
        if (pair.cameraPairInd.size() >= 2) {
            stitched.pairIndices[(ipair) * 2] = pair.cameraPairInd[0];
            stitched.pairIndices[(ipair) * 2 + 1] = pair.cameraPairInd[1];
        }
        
        std::cout << "  Pair " << (ipair + 1) << ": " << pair_nPoints << " points, " 
                  << pair_nFaces << " faces (appended)" << std::endl;
    }
    
    // Merge calibration data from individual pairs
    for (const auto& pair : all_pairs) {
        // Each pair has DLTpath and DLTparameters as vectors (2 cameras)
        // Convert to 2D structure for DIC3Dcombined
        stitched.calibration.DLT_paths.push_back(pair.DLTpath);
        stitched.calibration.DLT_params.push_back(pair.DLTparameters);
    }
    
    // Merge distortion data from individual pairs
    for (const auto& pair : all_pairs) {
        stitched.distortion.distortion_models.push_back(pair.distortionModel);
        stitched.distortion.distortion_paths.push_back(pair.distortionPath);
    }
    
    size_t total_points = stitched.Points3D[0].x.size();
    size_t total_faces = stitched.Faces.size() / 3;
    std::cout << "  Total stitched: " << total_points << " points, " 
              << total_faces << " faces" << std::endl;
    
    return stitched;
}

// ============================================================================
// Helper Functions for Geometric Stitching
// ============================================================================

std::vector<int> computeMeshBoundary(const std::vector<int>& faces,
                                      const std::vector<Eigen::Vector3d>& vertices) {
    if (faces.size() % 3 != 0) {
        std::cerr << "Error: faces must be divisible by 3" << std::endl;
        return {};
    }
    
    size_t nFaces = faces.size() / 3;
    
    // Count edge occurrences (boundary edges appear only once)
    std::map<std::pair<int, int>, int> edge_count;
    
    for (size_t i = 0; i < nFaces; ++i) {
        int v0 = faces[i * 3 + 0];
        int v1 = faces[i * 3 + 1];
        int v2 = faces[i * 3 + 2];
        
        if (static_cast<size_t>(v0) >= vertices.size() || static_cast<size_t>(v1) >= vertices.size() || static_cast<size_t>(v2) >= vertices.size()) {
            continue;
        }
        if (vertices[v0].hasNaN() || vertices[v1].hasNaN() || vertices[v2].hasNaN()) {
            continue;
        }
        
        auto add_edge = [&](int a, int b) {
            auto key = (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
            edge_count[key]++;
        };
        
        add_edge(v0, v1);
        add_edge(v1, v2);
        add_edge(v2, v0);
    }
    
    std::vector<int> boundary_edges;
    for (const auto& [edge, count] : edge_count) {
        if (count == 1) {
            boundary_edges.push_back(edge.first);
            boundary_edges.push_back(edge.second);
        }
    }
    
    return boundary_edges;
}

std::vector<double> computeEdgeLengths(const std::vector<int>& faces,
                                        const std::vector<Eigen::Vector3d>& vertices) {
    std::vector<double> edge_lengths;
    if (faces.size() % 3 != 0) return edge_lengths;
    
    size_t nFaces = faces.size() / 3;
    edge_lengths.reserve(nFaces * 3);
    
    for (size_t i = 0; i < nFaces; ++i) {
        int v0 = faces[i * 3 + 0];
        int v1 = faces[i * 3 + 1];
        int v2 = faces[i * 3 + 2];
        
        if (static_cast<size_t>(v0) >= vertices.size() || static_cast<size_t>(v1) >= vertices.size() || static_cast<size_t>(v2) >= vertices.size()) {
            edge_lengths.push_back(std::nan(""));
            edge_lengths.push_back(std::nan(""));
            edge_lengths.push_back(std::nan(""));
            continue;
        }
        
        edge_lengths.push_back((vertices[v1] - vertices[v0]).norm());
        edge_lengths.push_back((vertices[v2] - vertices[v1]).norm());
        edge_lengths.push_back((vertices[v0] - vertices[v2]).norm());
    }
    
    return edge_lengths;
}

// ============================================================================
// MATLAB-faithful overlap removal helpers
// ============================================================================

// Per-vertex area-weighted normal. Matches MATLAB patchNormal(F,V).
// Each face's geometric normal (B-A)x(C-A) has magnitude == 2*area, so summing
// over incident faces yields an area-weighted average; we then unit-normalize.
static std::vector<Eigen::Vector3d> computeVertexNormals(
    const std::vector<int>& faces,
    const std::vector<Eigen::Vector3d>& vertices) {

    std::vector<Eigen::Vector3d> normals(vertices.size(), Eigen::Vector3d::Zero());
    size_t nF = faces.size() / 3;
    for (size_t i = 0; i < nF; ++i) {
        int v0 = faces[i*3], v1 = faces[i*3+1], v2 = faces[i*3+2];
        if (v0 < 0 || v1 < 0 || v2 < 0) continue;
        if (static_cast<size_t>(v0) >= vertices.size() ||
            static_cast<size_t>(v1) >= vertices.size() ||
            static_cast<size_t>(v2) >= vertices.size()) continue;
        const auto& A = vertices[v0];
        const auto& B = vertices[v1];
        const auto& C = vertices[v2];
        if (A.hasNaN() || B.hasNaN() || C.hasNaN()) continue;
        Eigen::Vector3d n = (B - A).cross(C - A); // area-weighted
        normals[v0] += n;
        normals[v1] += n;
        normals[v2] += n;
    }
    for (auto& n : normals) {
        double nm = n.norm();
        if (nm > 0.0) n /= nm;
    }
    return normals;
}

// Möller–Trumbore ray-triangle intersection. Two-sided triangle (no backface
// culling); semi-infinite ray (t >= 0). Returns parametric t along D, or NaN.
static double rayTriangleHit(const Eigen::Vector3d& O,
                             const Eigen::Vector3d& D,
                             const Eigen::Vector3d& A,
                             const Eigen::Vector3d& B,
                             const Eigen::Vector3d& C,
                             double eps) {
    Eigen::Vector3d E1 = B - A;
    Eigen::Vector3d E2 = C - A;
    Eigen::Vector3d P = D.cross(E2);
    double det = E1.dot(P);
    if (std::abs(det) < eps) return std::nan(""); // ray parallel to triangle
    double inv_det = 1.0 / det;
    Eigen::Vector3d T = O - A;
    double u = T.dot(P) * inv_det;
    if (u < 0.0 || u > 1.0) return std::nan("");
    Eigen::Vector3d Q = T.cross(E1);
    double v = D.dot(Q) * inv_det;
    if (v < 0.0 || u + v > 1.0) return std::nan("");
    double t = E2.dot(Q) * inv_det;
    if (t < 0.0) return std::nan(""); // semi-infinite ray
    return t;
}

// For each (origin, direction) pair, find closest hit point on the mesh.
// Returns hit positions; NaN-filled Vector3d when no hit.
// Matches MATLAB triSurfRaySetIntersect with optStruct.ray='ray', triangle='two sided'.
static std::vector<Eigen::Vector3d> raySetTriangleIntersect(
    const std::vector<Eigen::Vector3d>& origins,
    const std::vector<Eigen::Vector3d>& dirs,
    const std::vector<int>& faces,
    const std::vector<Eigen::Vector3d>& vertices,
    double eps = 1e-6) {

    std::vector<Eigen::Vector3d> hits(origins.size(),
                                      Eigen::Vector3d::Constant(std::nan("")));
    size_t nF = faces.size() / 3;
    for (size_t r = 0; r < origins.size(); ++r) {
        const auto& O = origins[r];
        const auto& D = dirs[r];
        if (O.hasNaN() || D.hasNaN() || D.norm() < 1e-12) continue;
        double best_t = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < nF; ++i) {
            int v0 = faces[i*3], v1 = faces[i*3+1], v2 = faces[i*3+2];
            if (v0 < 0 || v1 < 0 || v2 < 0) continue;
            if (static_cast<size_t>(v0) >= vertices.size() ||
                static_cast<size_t>(v1) >= vertices.size() ||
                static_cast<size_t>(v2) >= vertices.size()) continue;
            const auto& A = vertices[v0];
            const auto& B = vertices[v1];
            const auto& C = vertices[v2];
            if (A.hasNaN() || B.hasNaN() || C.hasNaN()) continue;
            double t = rayTriangleHit(O, D, A, B, C, eps);
            if (!std::isnan(t) && t < best_t) best_t = t;
        }
        if (std::isfinite(best_t)) hits[r] = O + best_t * D;
    }
    return hits;
}

// triSurfSetDist(F1,V1,F2,V2,'ray'): per-vertex ray-trace distance from V1
// (along its outward normal) onto surface (F2,V2). NaN if no hit.
static std::vector<double> computeRaySurfaceDistance(
    const std::vector<Eigen::Vector3d>& V1,
    const std::vector<Eigen::Vector3d>& N1,
    const std::vector<int>& F2,
    const std::vector<Eigen::Vector3d>& V2) {

    auto hits = raySetTriangleIntersect(V1, N1, F2, V2, 1e-6);
    std::vector<double> Q(V1.size(), std::nan(""));
    for (size_t i = 0; i < V1.size(); ++i) {
        if (!hits[i].hasNaN()) Q[i] = (V1[i] - hits[i]).norm();
    }
    return Q;
}

// Mean of finite triangle edge lengths (NaN-safe).
static double meanFiniteEdgeLength(const std::vector<int>& faces,
                                   const std::vector<Eigen::Vector3d>& vertices) {
    auto el = computeEdgeLengths(faces, vertices);
    double sum = 0.0;
    size_t n = 0;
    for (double e : el) { if (std::isfinite(e)) { sum += e; ++n; } }
    return n ? sum / static_cast<double>(n) : 0.0;
}

// Filter face list by per-face keep mask (flat [v0,v1,v2, ...] in/out).
static std::vector<int> applyFaceMask(const std::vector<int>& F,
                                      const std::vector<bool>& CT) {
    std::vector<int> out;
    size_t nF = F.size() / 3;
    out.reserve(F.size());
    for (size_t i = 0; i < nF; ++i) {
        if (CT[i]) {
            out.push_back(F[i*3]);
            out.push_back(F[i*3+1]);
            out.push_back(F[i*3+2]);
        }
    }
    return out;
}

// Mark every still-active face that references v_remove as removed (CT[i]=false).
// Matches MATLAB: logicF1Remove = ~any(logicV1Remove(F1),2); CT1 = CT1 & logicF1Remove.
static bool removeFacesContainingVertex(const std::vector<int>& F,
                                        std::vector<bool>& CT,
                                        int v_remove) {
    size_t nF = F.size() / 3;
    bool removed_any = false;
    for (size_t i = 0; i < nF; ++i) {
        if (!CT[i]) continue;
        if (F[i*3] == v_remove || F[i*3+1] == v_remove || F[i*3+2] == v_remove) {
            CT[i] = false;
            removed_any = true;
        }
    }
    return removed_any;
}

// ============================================================================
// Faithful port of removeOverlapSurface_temp_2018_10_30.m
//
// Two phase iterative erosion:
//   Phase 1: ray-trace boundary vertices along their normals onto the other
//     surface. A boundary vertex is "overlapping" if it ray-hits the other
//     surface within 2*meanEdgeLength. Each iteration removes ALL active faces
//     containing the worst-quality boundary vertex (the one with the largest
//     pre-computed ray distance Q). Removal happens on the surface whose worst
//     Q is larger; if pair_forced is true, we always remove from surface 2.
//   Phase 2: gap closure. Using Euclidean min-distance from each boundary
//     vertex to the other boundary's vertices PLUS its edge midpoints and
//     1/4 / 3/4 points (closer-than-vertex sampling), remove faces while any
//     boundary vertex sits within min_gap of the other boundary.
//
// min_gap should be 0.4 * meanEdgeLength (MATLAB caller convention).
// ============================================================================
std::pair<std::vector<bool>, std::vector<bool>> removeOverlapSurfaces(
    const std::vector<int>& faces1,
    const std::vector<int>& faces2,
    const std::vector<Eigen::Vector3d>& V1,
    const std::vector<Eigen::Vector3d>& V2,
    double min_gap,
    bool pair_forced) {

    const size_t nF1 = faces1.size() / 3;
    const size_t nF2 = faces2.size() / 3;
    std::vector<bool> CT1(nF1, true);
    std::vector<bool> CT2(nF2, true);
    if (nF1 == 0 || nF2 == 0) return {CT1, CT2};

    // Per-vertex normals + quality measure Q (ray distance) — computed once on
    // the original full meshes, matching MATLAB lines 22-25, 60-62.
    auto N1 = computeVertexNormals(faces1, V1);
    auto N2 = computeVertexNormals(faces2, V2);
    auto Q1 = computeRaySurfaceDistance(V1, N1, faces2, V2);
    auto Q2 = computeRaySurfaceDistance(V2, N2, faces1, V1);

    const double meanEdge =
        0.5 * (meanFiniteEdgeLength(faces1, V1) + meanFiniteEdgeLength(faces2, V2));
    if (meanEdge <= 0.0) return {CT1, CT2};
    const double rayCloseThresh = 2.0 * meanEdge;

    std::vector<bool> logicNoHit1(V1.size(), false);
    std::vector<bool> logicNoHit2(V2.size(), false);

    // Safety bound: in the worst case each iteration removes one face.
    const size_t maxIter = nF1 + nF2 + 16;
    size_t removed_phase1_s1 = 0, removed_phase1_s2 = 0;
    size_t removed_phase2_s1 = 0, removed_phase2_s2 = 0;

    // ---- Phase 1: ray-based overlap removal ----
    for (size_t iter = 0; iter < maxIter; ++iter) {
        auto F1cur = applyFaceMask(faces1, CT1);
        auto F2cur = applyFaceMask(faces2, CT2);
        if (F1cur.empty() || F2cur.empty()) break;

        auto Eb1 = computeMeshBoundary(F1cur, V1);
        auto Eb2 = computeMeshBoundary(F2cur, V2);

        // Boundary vertex sets (unique), excluding previously-no-hit vertices.
        std::set<int> b1set, b2set;
        for (int v : Eb1) b1set.insert(v);
        for (int v : Eb2) b2set.insert(v);
        std::vector<int> ib1, ib2;
        ib1.reserve(b1set.size()); ib2.reserve(b2set.size());
        for (int v : b1set)
            if (static_cast<size_t>(v) < logicNoHit1.size() && !logicNoHit1[v])
                ib1.push_back(v);
        for (int v : b2set)
            if (static_cast<size_t>(v) < logicNoHit2.size() && !logicNoHit2[v])
                ib2.push_back(v);

        if (ib1.empty() && ib2.empty()) break;

        int worst_v1 = -1, first_close_v1 = -1;
        int worst_v2 = -1, first_close_v2 = -1;
        double worstQ1 = -std::numeric_limits<double>::infinity();
        double worstQ2 = -std::numeric_limits<double>::infinity();

        if (!ib1.empty()) {
            std::vector<Eigen::Vector3d> O(ib1.size()), D(ib1.size());
            for (size_t i = 0; i < ib1.size(); ++i) { O[i] = V1[ib1[i]]; D[i] = N1[ib1[i]]; }
            auto trace = raySetTriangleIntersect(O, D, F2cur, V2, 1e-6);
            for (size_t i = 0; i < ib1.size(); ++i) {
                bool close = !trace[i].hasNaN() &&
                             (V1[ib1[i]] - trace[i]).norm() < rayCloseThresh;
                if (!close) {
                    logicNoHit1[ib1[i]] = true;
                } else {
                    if (first_close_v1 < 0) first_close_v1 = ib1[i];
                    double q = Q1[ib1[i]];
                    if (std::isfinite(q) && q > worstQ1) { worstQ1 = q; worst_v1 = ib1[i]; }
                }
            }
        }
        if (!ib2.empty()) {
            std::vector<Eigen::Vector3d> O(ib2.size()), D(ib2.size());
            for (size_t i = 0; i < ib2.size(); ++i) { O[i] = V2[ib2[i]]; D[i] = N2[ib2[i]]; }
            auto trace = raySetTriangleIntersect(O, D, F1cur, V1, 1e-6);
            for (size_t i = 0; i < ib2.size(); ++i) {
                bool close = !trace[i].hasNaN() &&
                             (V2[ib2[i]] - trace[i]).norm() < rayCloseThresh;
                if (!close) {
                    logicNoHit2[ib2[i]] = true;
                } else {
                    if (first_close_v2 < 0) first_close_v2 = ib2[i];
                    double q = Q2[ib2[i]];
                    if (std::isfinite(q) && q > worstQ2) { worstQ2 = q; worst_v2 = ib2[i]; }
                }
            }
        }

        bool any_close_1 = (first_close_v1 >= 0);
        bool any_close_2 = (first_close_v2 >= 0);
        if (!any_close_1 && !any_close_2) break;

        // Pick which surface to erode this iteration.
        bool remove_from_1;
        if (any_close_1 && any_close_2) {
            // pair_forced -> always favor removing from surface 2 (MATLAB convention)
            remove_from_1 = (!pair_forced) && (worstQ1 > worstQ2);
        } else {
            remove_from_1 = any_close_1;
        }

        int v_remove = remove_from_1 ? (worst_v1 >= 0 ? worst_v1 : first_close_v1)
                                     : (worst_v2 >= 0 ? worst_v2 : first_close_v2);
        if (v_remove < 0) break;

        bool ok = remove_from_1
            ? removeFacesContainingVertex(faces1, CT1, v_remove)
            : removeFacesContainingVertex(faces2, CT2, v_remove);
        if (!ok) break;
        if (remove_from_1) ++removed_phase1_s1; else ++removed_phase1_s2;
    }

    // ---- Phase 2: gap closure ----
    // Iteratively remove the worst boundary face while either boundary sits
    // within min_gap of the other (Euclidean distance to vertices + edge
    // midpoints + 1/4 / 3/4 points). pair_forced is NOT considered in phase 2
    // (matches MATLAB line 324: `if worstQ1>worstQ2` without pair_forced guard).
    {
        auto F1cur = applyFaceMask(faces1, CT1);
        auto F2cur = applyFaceMask(faces2, CT2);
        if (F1cur.empty() || F2cur.empty()) {
            // Nothing to do in phase 2.
        } else {
            for (size_t iter = 0; iter < maxIter; ++iter) {
                F1cur = applyFaceMask(faces1, CT1);
                F2cur = applyFaceMask(faces2, CT2);
                if (F1cur.empty() || F2cur.empty()) break;

                auto Eb1 = computeMeshBoundary(F1cur, V1);
                auto Eb2 = computeMeshBoundary(F2cur, V2);
                if (Eb1.empty() || Eb2.empty()) break;

                std::set<int> b1set, b2set;
                for (int v : Eb1) b1set.insert(v);
                for (int v : Eb2) b2set.insert(v);
                std::vector<int> ib1(b1set.begin(), b1set.end());
                std::vector<int> ib2(b2set.begin(), b2set.end());

                // Build extended target sets (boundary verts + edge midpoints + 1/4,3/4).
                std::vector<Eigen::Vector3d> tgt1, tgt2;
                tgt1.reserve(ib1.size() + (Eb1.size() / 2) * 3);
                tgt2.reserve(ib2.size() + (Eb2.size() / 2) * 3);
                for (int v : ib1) tgt1.push_back(V1[v]);
                for (int v : ib2) tgt2.push_back(V2[v]);
                size_t nEb1 = Eb1.size() / 2, nEb2 = Eb2.size() / 2;
                for (size_t i = 0; i < nEb1; ++i) {
                    int a = Eb1[i*2], b = Eb1[i*2+1];
                    if (static_cast<size_t>(a) >= V1.size() ||
                        static_cast<size_t>(b) >= V1.size()) continue;
                    const auto& A = V1[a]; const auto& B = V1[b];
                    if (A.hasNaN() || B.hasNaN()) continue;
                    tgt1.push_back(0.5  * (A + B));
                    tgt1.push_back(0.25 * A + 0.75 * B);
                    tgt1.push_back(0.75 * A + 0.25 * B);
                }
                for (size_t i = 0; i < nEb2; ++i) {
                    int a = Eb2[i*2], b = Eb2[i*2+1];
                    if (static_cast<size_t>(a) >= V2.size() ||
                        static_cast<size_t>(b) >= V2.size()) continue;
                    const auto& A = V2[a]; const auto& B = V2[b];
                    if (A.hasNaN() || B.hasNaN()) continue;
                    tgt2.push_back(0.5  * (A + B));
                    tgt2.push_back(0.25 * A + 0.75 * B);
                    tgt2.push_back(0.75 * A + 0.25 * B);
                }

                int worst_v1 = -1, first_close_v1 = -1;
                int worst_v2 = -1, first_close_v2 = -1;
                double worstQ1 = -std::numeric_limits<double>::infinity();
                double worstQ2 = -std::numeric_limits<double>::infinity();
                size_t close1 = 0, close2 = 0;

                for (int v : ib1) {
                    const auto& P = V1[v];
                    if (P.hasNaN()) continue;
                    double dmin = std::numeric_limits<double>::infinity();
                    for (const auto& p : tgt2) {
                        if (p.hasNaN()) continue;
                        double d = (P - p).norm();
                        if (d < dmin) dmin = d;
                    }
                    if (dmin < min_gap) {
                        ++close1;
                        if (first_close_v1 < 0) first_close_v1 = v;
                        double q = Q1[v];
                        if (std::isfinite(q) && q > worstQ1) { worstQ1 = q; worst_v1 = v; }
                    }
                }
                for (int v : ib2) {
                    const auto& P = V2[v];
                    if (P.hasNaN()) continue;
                    double dmin = std::numeric_limits<double>::infinity();
                    for (const auto& p : tgt1) {
                        if (p.hasNaN()) continue;
                        double d = (P - p).norm();
                        if (d < dmin) dmin = d;
                    }
                    if (dmin < min_gap) {
                        ++close2;
                        if (first_close_v2 < 0) first_close_v2 = v;
                        double q = Q2[v];
                        if (std::isfinite(q) && q > worstQ2) { worstQ2 = q; worst_v2 = v; }
                    }
                }

                if (close1 == 0 && close2 == 0) break;

                bool remove_from_1;
                if (close1 > 0 && close2 > 0) {
                    remove_from_1 = (worstQ1 > worstQ2);
                } else {
                    remove_from_1 = (close1 > 0);
                }

                int v_remove = remove_from_1
                    ? (worst_v1 >= 0 ? worst_v1 : first_close_v1)
                    : (worst_v2 >= 0 ? worst_v2 : first_close_v2);
                if (v_remove < 0) break;

                bool ok = remove_from_1
                    ? removeFacesContainingVertex(faces1, CT1, v_remove)
                    : removeFacesContainingVertex(faces2, CT2, v_remove);
                if (!ok) break;
                if (remove_from_1) ++removed_phase2_s1; else ++removed_phase2_s2;
            }
        }
    }

    std::cout << "    removeOverlapSurfaces: phase1 removed " << removed_phase1_s1
              << " faces from S1, " << removed_phase1_s2 << " from S2;"
              << " phase2 removed " << removed_phase2_s1 << " from S1, "
              << removed_phase2_s2 << " from S2 (meanEdge=" << meanEdge
              << ", minGap=" << min_gap << ")" << std::endl;

    return {CT1, CT2};
}

// Group boundary edges into connected components (matches MATLAB tesgroup)
std::vector<std::vector<int>> groupBoundaryEdges(const std::vector<int>& boundary_edges) {
    size_t nEdges = boundary_edges.size() / 2;
    if (nEdges == 0) return {};
    
    // Build adjacency: vertex -> list of edge indices
    std::unordered_map<int, std::vector<int>> vert_to_edges;
    for (size_t i = 0; i < nEdges; ++i) {
        vert_to_edges[boundary_edges[i*2]].push_back(static_cast<int>(i));
        vert_to_edges[boundary_edges[i*2+1]].push_back(static_cast<int>(i));
    }
    
    std::vector<bool> visited(nEdges, false);
    std::vector<std::vector<int>> groups;
    
    for (size_t start = 0; start < nEdges; ++start) {
        if (visited[start]) continue;
        
        // BFS from this edge
        std::vector<int> group;
        std::vector<int> stack = {static_cast<int>(start)};
        visited[start] = true;
        
        while (!stack.empty()) {
            int eidx = stack.back();
            stack.pop_back();
            group.push_back(eidx);
            
            // Visit edges connected through shared vertices
            for (int k = 0; k < 2; ++k) {
                int v = boundary_edges[eidx * 2 + k];
                for (int neighbor : vert_to_edges[v]) {
                    if (!visited[neighbor]) {
                        visited[neighbor] = true;
                        stack.push_back(neighbor);
                    }
                }
            }
        }
        groups.push_back(std::move(group));
    }
    
    return groups;
}

// Convert unordered edge list to ordered vertex curve (matches MATLAB edgeListToCurve)
std::vector<int> edgeListToCurve(const std::vector<int>& edges) {
    size_t nEdges = edges.size() / 2;
    if (nEdges == 0) return {};
    
    // Build adjacency list
    std::unordered_map<int, std::vector<int>> adj;
    for (size_t i = 0; i < nEdges; ++i) {
        int a = edges[i*2], b = edges[i*2+1];
        adj[a].push_back(b);
        adj[b].push_back(a);
    }
    
    // Start from first vertex of first edge
    int start = edges[0];
    std::vector<int> curve;
    curve.push_back(start);
    
    std::set<int> visited_verts;
    visited_verts.insert(start);
    int current = start;
    
    while (true) {
        bool found = false;
        for (int next : adj[current]) {
            if (visited_verts.find(next) == visited_verts.end()) {
                curve.push_back(next);
                visited_verts.insert(next);
                current = next;
                found = true;
                break;
            }
        }
        if (!found) break;
    }
    
    return curve;
}

// Find faces where all 3 edges are boundary edges
std::vector<bool> findAllBoundaryFaces(const std::vector<int>& faces,
                                        const std::vector<int>& boundary_edges) {
    size_t nFaces = faces.size() / 3;
    
    // Build set of boundary edges (as sorted pairs)
    std::set<std::pair<int,int>> boundary_set;
    for (size_t i = 0; i < boundary_edges.size() / 2; ++i) {
        int a = boundary_edges[i*2], b = boundary_edges[i*2+1];
        boundary_set.insert(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
    }
    
    auto is_boundary_edge = [&](int a, int b) -> bool {
        auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
        return boundary_set.count(key) > 0;
    };
    
    std::vector<bool> keep(nFaces, true);
    for (size_t i = 0; i < nFaces; ++i) {
        int v0 = faces[i*3], v1 = faces[i*3+1], v2 = faces[i*3+2];
        if (is_boundary_edge(v0, v1) && is_boundary_edge(v1, v2) && is_boundary_edge(v0, v2)) {
            keep[i] = false;
        }
    }
    return keep;
}

// Zip two boundary curves with a greedy advancing-front triangle strip
std::vector<int> zipBoundaryCurves(const std::vector<int>& curve1,
                                    const std::vector<int>& curve2,
                                    const std::vector<Eigen::Vector3d>& V_combined) {
    if (curve1.size() < 2 || curve2.size() < 2) return {};
    
    std::vector<int> new_faces;
    size_t i1 = 0, i2 = 0;
    
    while (i1 < curve1.size() - 1 || i2 < curve2.size() - 1) {
        int c1_cur = curve1[i1];
        int c2_cur = curve2[i2];
        
        bool can_advance_1 = (i1 < curve1.size() - 1);
        bool can_advance_2 = (i2 < curve2.size() - 1);
        
        if (!can_advance_1 && !can_advance_2) break;
        
        bool advance_1 = false;
        if (can_advance_1 && can_advance_2) {
            // Pick shorter diagonal
            int c1_next = curve1[i1 + 1];
            int c2_next = curve2[i2 + 1];
            double d1 = (V_combined[c1_next] - V_combined[c2_cur]).norm();
            double d2 = (V_combined[c2_next] - V_combined[c1_cur]).norm();
            advance_1 = (d1 <= d2);
        } else {
            advance_1 = can_advance_1;
        }
        
        if (advance_1) {
            int c1_next = curve1[i1 + 1];
            new_faces.push_back(c1_cur);
            new_faces.push_back(c1_next);
            new_faces.push_back(c2_cur);
            ++i1;
        } else {
            int c2_next = curve2[i2 + 1];
            new_faces.push_back(c1_cur);
            new_faces.push_back(c2_next);
            new_faces.push_back(c2_cur);
            ++i2;
        }
    }
    
    return new_faces;
}

// ============================================================================
// Internal: Filter faces by keep mask, return new face list
// ============================================================================
static std::vector<int> filterFaces(const std::vector<int>& faces,
                                     const std::vector<bool>& keep) {
    std::vector<int> result;
    size_t nFaces = faces.size() / 3;
    for (size_t i = 0; i < nFaces; ++i) {
        if (keep[i]) {
            result.push_back(faces[i*3]);
            result.push_back(faces[i*3+1]);
            result.push_back(faces[i*3+2]);
        }
    }
    return result;
}

// Filter a per-face vector by keep mask
template<typename T>
static std::vector<T> filterByMask(const std::vector<T>& data, const std::vector<bool>& keep) {
    std::vector<T> result;
    for (size_t i = 0; i < keep.size() && i < data.size(); ++i) {
        if (keep[i]) result.push_back(data[i]);
    }
    return result;
}

// ============================================================================
// Geometric Stitching Implementation
// ============================================================================

DIC3Dcombined stitchPairsGeometric(const std::vector<DIC3DpairResults>& all_pairs,
                                    const std::vector<int>& pair_order,
                                    bool pair_forced) {
    if (all_pairs.empty()) {
        return DIC3Dcombined();
    }
    
    if (pair_order.empty()) {
        std::cout << "  Warning: Empty pair order, using simple stitching" << std::endl;
        return stitchPairsSimple(all_pairs);
    }
    
    std::cout << "\n=== Geometric Stitching (with overlap removal) ===" << std::endl;
    std::cout << "  Stitching order: ";
    for (int idx : pair_order) std::cout << idx << " ";
    std::cout << std::endl;
    std::cout << "  pair_forced=" << (pair_forced ? "true" : "false") << std::endl;
    
    size_t first_idx = pair_order[0] - 1;
    if (first_idx >= all_pairs.size()) {
        std::cerr << "Error: Invalid pair index in pair_order" << std::endl;
        return DIC3Dcombined();
    }
    
    size_t nPairs = all_pairs.size();
    size_t nFrames = all_pairs[first_idx].Points3D.size();
    
    // === Working state: faces, per-face data, vertices ===
    // We maintain a "current stitched" mesh and iteratively add pairs.
    // Faces index into the combined vertex array [V_stitched ; V_next].
    
    // Initialize from first pair
    std::vector<int> cur_faces = all_pairs[first_idx].Faces;
    std::vector<double> cur_faceColors = all_pairs[first_idx].FaceColors;
    std::vector<int> cur_facePairInds(cur_faces.size() / 3, pair_order[0]);
    // Per-frame data: Points3D, corrComb stored as vectors-of-frames
    std::vector<Points3D> cur_pts3d = all_pairs[first_idx].Points3D;
    std::vector<std::vector<double>> cur_corrComb = all_pairs[first_idx].corrComb;
    std::vector<int> cur_pointPairInds(cur_pts3d[0].x.size(), pair_order[0]);
    
    // pairIndices matrix (nPairs x 2 flat)
    std::vector<int> pairIndices(nPairs * 2, 0);
    if (all_pairs[first_idx].cameraPairInd.size() >= 2) {
        pairIndices[first_idx * 2] = all_pairs[first_idx].cameraPairInd[0];
        pairIndices[first_idx * 2 + 1] = all_pairs[first_idx].cameraPairInd[1];
    }
    
    std::cout << "  Pair " << pair_order[0] << " (base): "
              << cur_pts3d[0].x.size() << " points, "
              << cur_faces.size() / 3 << " faces" << std::endl;
    
    // === Iteratively stitch remaining pairs ===
    for (size_t ipair = 1; ipair < pair_order.size(); ++ipair) {
        size_t pair_idx = pair_order[ipair] - 1;
        if (pair_idx >= all_pairs.size()) {
            std::cerr << "Error: Invalid pair index " << pair_order[ipair] << std::endl;
            continue;
        }
        
        const auto& next = all_pairs[pair_idx];
        std::vector<int> next_faces = next.Faces;
        std::vector<double> next_faceColors = next.FaceColors;
        
        // Convert to Eigen vectors (frame 0) for geometry operations
        size_t nV1 = cur_pts3d[0].x.size();
        size_t nV2 = next.Points3D[0].x.size();
        std::vector<Eigen::Vector3d> V1(nV1), V2(nV2);
        for (size_t i = 0; i < nV1; ++i)
            V1[i] = Eigen::Vector3d(cur_pts3d[0].x[i], cur_pts3d[0].y[i], cur_pts3d[0].z[i]);
        for (size_t i = 0; i < nV2; ++i)
            V2[i] = Eigen::Vector3d(next.Points3D[0].x[i], next.Points3D[0].y[i], next.Points3D[0].z[i]);
        
        // ---- Step 1: Remove NaN faces ----
        {
            size_t nF1 = cur_faces.size() / 3;
            std::vector<bool> keep1(nF1, true);
            for (size_t i = 0; i < nF1; ++i) {
                int v0=cur_faces[i*3], v1=cur_faces[i*3+1], v2=cur_faces[i*3+2];
                if (static_cast<size_t>(v0)>=V1.size()||static_cast<size_t>(v1)>=V1.size()||static_cast<size_t>(v2)>=V1.size()||
                    V1[v0].hasNaN()||V1[v1].hasNaN()||V1[v2].hasNaN())
                    keep1[i] = false;
            }
            cur_faces = filterFaces(cur_faces, keep1);
            cur_faceColors = filterByMask(cur_faceColors, keep1);
            cur_facePairInds = filterByMask(cur_facePairInds, keep1);
        }
        {
            size_t nF2 = next_faces.size() / 3;
            std::vector<bool> keep2(nF2, true);
            for (size_t i = 0; i < nF2; ++i) {
                int v0=next_faces[i*3], v1=next_faces[i*3+1], v2=next_faces[i*3+2];
                if (static_cast<size_t>(v0)>=V2.size()||static_cast<size_t>(v1)>=V2.size()||static_cast<size_t>(v2)>=V2.size()||
                    V2[v0].hasNaN()||V2[v1].hasNaN()||V2[v2].hasNaN())
                    keep2[i] = false;
            }
            next_faces = filterFaces(next_faces, keep2);
            next_faceColors = filterByMask(next_faceColors, keep2);
        }
        
        // ---- Step 2: Overlap removal ----
        auto edgeLens1 = computeEdgeLengths(cur_faces, V1);
        auto edgeLens2 = computeEdgeLengths(next_faces, V2);
        double meanEdge = 0.0;
        size_t cnt = 0;
        for (double e : edgeLens1) { if (!std::isnan(e)) { meanEdge += e; cnt++; } }
        for (double e : edgeLens2) { if (!std::isnan(e)) { meanEdge += e; cnt++; } }
        if (cnt > 0) meanEdge /= cnt;
        
        double minGap = 0.4 * meanEdge;
        double minDistValue = 5.0 * minGap;  // used later for boundary zipping
        
        // MATLAB-faithful overlap removal: iterative ray-tracing erosion in phase 1,
        // gap closure with min_gap in phase 2. Pass minGap (NOT minDistValue).
        auto [keep1, keep2] = removeOverlapSurfaces(
            cur_faces, next_faces, V1, V2, minGap, pair_forced);
        
        size_t rem1 = std::count(keep1.begin(), keep1.end(), false);
        size_t rem2 = std::count(keep2.begin(), keep2.end(), false);
        std::cout << "    Overlap removal: " << rem1 << " faces from S1, " << rem2 << " from S2" << std::endl;
        
        // Apply overlap filter
        cur_faces = filterFaces(cur_faces, keep1);
        cur_faceColors = filterByMask(cur_faceColors, keep1);
        cur_facePairInds = filterByMask(cur_facePairInds, keep1);
        next_faces = filterFaces(next_faces, keep2);
        next_faceColors = filterByMask(next_faceColors, keep2);
        
        // ---- Step 3: Remove all-boundary faces ----
        {
            auto bnd1 = computeMeshBoundary(cur_faces, V1);
            auto abf1 = findAllBoundaryFaces(cur_faces, bnd1);
            size_t remB1 = std::count(abf1.begin(), abf1.end(), false);
            if (remB1 > 0) {
                cur_faces = filterFaces(cur_faces, abf1);
                cur_faceColors = filterByMask(cur_faceColors, abf1);
                cur_facePairInds = filterByMask(cur_facePairInds, abf1);
                std::cout << "    Removed " << remB1 << " all-boundary faces from S1" << std::endl;
            }
        }
        {
            auto bnd2 = computeMeshBoundary(next_faces, V2);
            auto abf2 = findAllBoundaryFaces(next_faces, bnd2);
            size_t remB2 = std::count(abf2.begin(), abf2.end(), false);
            if (remB2 > 0) {
                next_faces = filterFaces(next_faces, abf2);
                next_faceColors = filterByMask(next_faceColors, abf2);
                std::cout << "    Removed " << remB2 << " all-boundary faces from S2" << std::endl;
            }
        }
        
        // ---- Step 4: Boundary zipping ----
        // Recompute boundaries after filtering
        auto bnd1 = computeMeshBoundary(cur_faces, V1);
        auto bnd2 = computeMeshBoundary(next_faces, V2);
        
        // Build combined vertex array for zipping
        // V2 vertices will be at offset nV1 in the combined array
        std::vector<Eigen::Vector3d> V_combined;
        V_combined.reserve(nV1 + nV2);
        V_combined.insert(V_combined.end(), V1.begin(), V1.end());
        V_combined.insert(V_combined.end(), V2.begin(), V2.end());
        
        // Offset next_faces for combined vertex array
        std::vector<int> next_faces_offset;
        next_faces_offset.reserve(next_faces.size());
        for (int idx : next_faces) next_faces_offset.push_back(idx + static_cast<int>(nV1));
        
        // Also offset bnd2 edges
        std::vector<int> bnd2_offset;
        bnd2_offset.reserve(bnd2.size());
        for (int idx : bnd2) bnd2_offset.push_back(idx + static_cast<int>(nV1));
        
        // Group boundaries
        auto groups1 = groupBoundaryEdges(bnd1);
        auto groups2 = groupBoundaryEdges(bnd2_offset);
        
        std::vector<int> zip_faces;
        
        for (size_t ib1 = 0; ib1 < groups1.size(); ++ib1) {
            // Extract edges for this group
            std::vector<int> edges_g1;
            for (int eidx : groups1[ib1]) {
                edges_g1.push_back(bnd1[eidx*2]);
                edges_g1.push_back(bnd1[eidx*2+1]);
            }
            auto curve1 = edgeListToCurve(edges_g1);
            if (curve1.size() < 2) continue;
            
            for (size_t ib2 = 0; ib2 < groups2.size(); ++ib2) {
                std::vector<int> edges_g2;
                for (int eidx : groups2[ib2]) {
                    edges_g2.push_back(bnd2_offset[eidx*2]);
                    edges_g2.push_back(bnd2_offset[eidx*2+1]);
                }
                auto curve2 = edgeListToCurve(edges_g2);
                if (curve2.size() < 2) continue;
                
                // Find minimum distance between boundary vertices of the two groups
                double group_min_dist = std::numeric_limits<double>::infinity();
                for (int v1_idx : curve1) {
                    if (V_combined[v1_idx].hasNaN()) continue;
                    for (int v2_idx : curve2) {
                        if (V_combined[v2_idx].hasNaN()) continue;
                        double d = (V_combined[v1_idx] - V_combined[v2_idx]).norm();
                        group_min_dist = std::min(group_min_dist, d);
                    }
                }
                
                // Only zip groups that are close enough
                if (group_min_dist > minDistValue) continue;
                
                // Find the close boundary segment on curve1
                // For each vertex in curve1, compute distance to closest vertex in curve2
                std::vector<double> d12(curve1.size(), std::numeric_limits<double>::infinity());
                for (size_t ci = 0; ci < curve1.size(); ++ci) {
                    if (V_combined[curve1[ci]].hasNaN()) continue;
                    for (int v2_idx : curve2) {
                        if (V_combined[v2_idx].hasNaN()) continue;
                        double d = (V_combined[curve1[ci]] - V_combined[v2_idx]).norm();
                        d12[ci] = std::min(d12[ci], d);
                    }
                }
                
                // Extract sub-curve of curve1 that is close to curve2
                std::vector<int> sub_curve1;
                for (size_t ci = 0; ci < curve1.size(); ++ci) {
                    if (d12[ci] < minDistValue) {
                        sub_curve1.push_back(curve1[ci]);
                    }
                }
                if (sub_curve1.size() < 2) continue;
                
                // Find corresponding sub-curve on curve2
                // For each vertex in sub_curve1, find closest vertex in curve2
                std::vector<size_t> closest_c2_indices;
                for (int v1_idx : sub_curve1) {
                    double best_d = std::numeric_limits<double>::infinity();
                    size_t best_j = 0;
                    for (size_t j = 0; j < curve2.size(); ++j) {
                        if (V_combined[curve2[j]].hasNaN()) continue;
                        double d = (V_combined[v1_idx] - V_combined[curve2[j]]).norm();
                        if (d < best_d) { best_d = d; best_j = j; }
                    }
                    closest_c2_indices.push_back(best_j);
                }
                
                // Determine range of curve2 indices
                size_t c2_min = *std::min_element(closest_c2_indices.begin(), closest_c2_indices.end());
                size_t c2_max = *std::max_element(closest_c2_indices.begin(), closest_c2_indices.end());
                
                std::vector<int> sub_curve2;
                for (size_t ci = c2_min; ci <= c2_max; ++ci) {
                    sub_curve2.push_back(curve2[ci]);
                }
                if (sub_curve2.size() < 2) continue;
                
                // Orient curve2 to match curve1 direction
                double d_start_start = (V_combined[sub_curve1.front()] - V_combined[sub_curve2.front()]).norm();
                double d_start_end = (V_combined[sub_curve1.front()] - V_combined[sub_curve2.back()]).norm();
                if (d_start_end < d_start_start) {
                    std::reverse(sub_curve2.begin(), sub_curve2.end());
                }
                
                // Trim endpoints: remove leading/trailing vertices if they make the gap worse
                while (sub_curve1.size() >= 2 && sub_curve2.size() >= 2) {
                    double d_cur = (V_combined[sub_curve1.front()] - V_combined[sub_curve2.front()]).norm();
                    double d_adv1 = (V_combined[sub_curve1[1]] - V_combined[sub_curve2.front()]).norm();
                    double d_adv2 = (V_combined[sub_curve1.front()] - V_combined[sub_curve2[1]]).norm();
                    if (d_adv1 < d_cur) { sub_curve1.erase(sub_curve1.begin()); }
                    else if (d_adv2 < d_cur) { sub_curve2.erase(sub_curve2.begin()); }
                    else break;
                }
                while (sub_curve1.size() >= 2 && sub_curve2.size() >= 2) {
                    double d_cur = (V_combined[sub_curve1.back()] - V_combined[sub_curve2.back()]).norm();
                    double d_adv1 = (V_combined[sub_curve1[sub_curve1.size()-2]] - V_combined[sub_curve2.back()]).norm();
                    double d_adv2 = (V_combined[sub_curve1.back()] - V_combined[sub_curve2[sub_curve2.size()-2]]).norm();
                    if (d_adv1 < d_cur) { sub_curve1.pop_back(); }
                    else if (d_adv2 < d_cur) { sub_curve2.pop_back(); }
                    else break;
                }
                
                if (sub_curve1.size() < 2 || sub_curve2.size() < 2) continue;
                
                // Zip the two sub-curves
                auto zipped = zipBoundaryCurves(sub_curve1, sub_curve2, V_combined);
                zip_faces.insert(zip_faces.end(), zipped.begin(), zipped.end());
            }
        }
        
        size_t nZipFaces = zip_faces.size() / 3;
        if (nZipFaces > 0) {
            std::cout << "    Zipped " << nZipFaces << " new faces between boundaries" << std::endl;
        }
        
        // ---- Step 5: Combine everything ----
        // Combined faces: cur_faces + next_faces_offset + zip_faces
        std::vector<int> combined_faces;
        combined_faces.insert(combined_faces.end(), cur_faces.begin(), cur_faces.end());
        combined_faces.insert(combined_faces.end(), next_faces_offset.begin(), next_faces_offset.end());
        combined_faces.insert(combined_faces.end(), zip_faces.begin(), zip_faces.end());
        
        // Combined face colors
        std::vector<double> combined_faceColors;
        combined_faceColors.insert(combined_faceColors.end(), cur_faceColors.begin(), cur_faceColors.end());
        combined_faceColors.insert(combined_faceColors.end(), next_faceColors.begin(), next_faceColors.end());
        // Zip faces get average color
        if (!cur_faceColors.empty() && !next_faceColors.empty()) {
            double avg_col = 0.0;
            for (double c : cur_faceColors) avg_col += c;
            for (double c : next_faceColors) avg_col += c;
            avg_col /= (cur_faceColors.size() + next_faceColors.size());
            combined_faceColors.insert(combined_faceColors.end(), nZipFaces, avg_col);
        }
        
        // Combined face pair indices
        std::vector<int> combined_facePairInds;
        combined_facePairInds.insert(combined_facePairInds.end(), cur_facePairInds.begin(), cur_facePairInds.end());
        combined_facePairInds.insert(combined_facePairInds.end(), next_faces.size() / 3, pair_order[ipair]);
        // Zip faces get a special pair index (nPairs + ipair, matching MATLAB)
        combined_facePairInds.insert(combined_facePairInds.end(), nZipFaces, static_cast<int>(nPairs + ipair));
        
        // Combined Points3D and corrComb for all frames
        std::vector<Points3D> combined_pts3d(nFrames);
        std::vector<std::vector<double>> combined_corrComb(nFrames);
        for (size_t f = 0; f < nFrames; ++f) {
            // Concatenate vertices: [current ; next]
            combined_pts3d[f].x = cur_pts3d[f].x;
            combined_pts3d[f].x.insert(combined_pts3d[f].x.end(), next.Points3D[f].x.begin(), next.Points3D[f].x.end());
            combined_pts3d[f].y = cur_pts3d[f].y;
            combined_pts3d[f].y.insert(combined_pts3d[f].y.end(), next.Points3D[f].y.begin(), next.Points3D[f].y.end());
            combined_pts3d[f].z = cur_pts3d[f].z;
            combined_pts3d[f].z.insert(combined_pts3d[f].z.end(), next.Points3D[f].z.begin(), next.Points3D[f].z.end());
            
            // NaN out vertices not referenced by any face (MATLAB lines 112-121: V1r(~V1rlogic,:)=NaN)
            std::set<int> referenced_verts;
            for (size_t fi = 0; fi < combined_faces.size(); ++fi) {
                referenced_verts.insert(combined_faces[fi]);
            }
            size_t totalV = combined_pts3d[f].x.size();
            for (size_t vi = 0; vi < totalV; ++vi) {
                if (referenced_verts.find(static_cast<int>(vi)) == referenced_verts.end()) {
                    combined_pts3d[f].x[vi] = std::nan("");
                    combined_pts3d[f].y[vi] = std::nan("");
                    combined_pts3d[f].z[vi] = std::nan("");
                }
            }
            
            // Correlation
            if (f < cur_corrComb.size() && f < next.corrComb.size()) {
                combined_corrComb[f] = cur_corrComb[f];
                combined_corrComb[f].insert(combined_corrComb[f].end(), next.corrComb[f].begin(), next.corrComb[f].end());
            }
        }
        
        // Combined point pair indices
        std::vector<int> combined_pointPairInds;
        combined_pointPairInds.insert(combined_pointPairInds.end(), cur_pointPairInds.begin(), cur_pointPairInds.end());
        combined_pointPairInds.insert(combined_pointPairInds.end(), nV2, pair_order[ipair]);
        
        // Update working state
        cur_faces = std::move(combined_faces);
        cur_faceColors = std::move(combined_faceColors);
        cur_facePairInds = std::move(combined_facePairInds);
        cur_pts3d = std::move(combined_pts3d);
        cur_corrComb = std::move(combined_corrComb);
        cur_pointPairInds = std::move(combined_pointPairInds);
        
        if (next.cameraPairInd.size() >= 2) {
            pairIndices[pair_idx * 2] = next.cameraPairInd[0];
            pairIndices[pair_idx * 2 + 1] = next.cameraPairInd[1];
        }
        
        std::cout << "  Pair " << pair_order[ipair] << " stitched: "
                  << cur_pts3d[0].x.size() << " points, "
                  << cur_faces.size() / 3 << " faces" << std::endl;
    }
    
    // ---- Step 6: Fill single-triangle holes ----
    {
        // Build combined V for boundary detection
        size_t totalV = cur_pts3d[0].x.size();
        std::vector<Eigen::Vector3d> V_final(totalV);
        for (size_t i = 0; i < totalV; ++i)
            V_final[i] = Eigen::Vector3d(cur_pts3d[0].x[i], cur_pts3d[0].y[i], cur_pts3d[0].z[i]);
        
        auto final_bnd = computeMeshBoundary(cur_faces, V_final);
        auto final_groups = groupBoundaryEdges(final_bnd);
        
        size_t filled = 0;
        for (const auto& group : final_groups) {
            // Extract edges for this group
            std::vector<int> edges_g;
            for (int eidx : group) {
                edges_g.push_back(final_bnd[eidx*2]);
                edges_g.push_back(final_bnd[eidx*2+1]);
            }
            
            // If exactly 3 edges -> single triangle hole
            if (edges_g.size() == 6) {  // 3 edges * 2 vertices
                std::set<int> verts_set;
                for (int v : edges_g) verts_set.insert(v);
                if (verts_set.size() == 3) {
                    auto it = verts_set.begin();
                    int v0 = *it++, v1 = *it++, v2 = *it;
                    cur_faces.push_back(v0);
                    cur_faces.push_back(v1);
                    cur_faces.push_back(v2);
                    
                    // Assign median pair index from adjacent faces
                    int fill_pair_ind = 0;
                    if (!cur_facePairInds.empty()) {
                        fill_pair_ind = cur_facePairInds[cur_facePairInds.size() / 2];
                    }
                    cur_facePairInds.push_back(fill_pair_ind);
                    
                    // Average face color
                    double fill_color = 128.0;
                    if (!cur_faceColors.empty()) {
                        double sum = 0;
                        for (double c : cur_faceColors) sum += c;
                        fill_color = sum / cur_faceColors.size();
                    }
                    cur_faceColors.push_back(fill_color);
                    filled++;
                }
            }
        }
        if (filled > 0) {
            std::cout << "  Filled " << filled << " single-triangle holes" << std::endl;
        }
    }
    
    // ---- Step 7: Append non-stitched pairs ----
    std::set<int> stitched_pairs_set(pair_order.begin(), pair_order.end());
    for (size_t ip = 0; ip < nPairs; ++ip) {
        int pairId = static_cast<int>(ip + 1);
        if (stitched_pairs_set.count(pairId)) continue;
        
        const auto& extra = all_pairs[ip];
        size_t voff = cur_pts3d[0].x.size();
        size_t extra_nFaces = extra.Faces.size() / 3;
        size_t extra_nVerts = extra.Points3D[0].x.size();
        
        // Append faces with offset
        for (int idx : extra.Faces) {
            cur_faces.push_back(idx + static_cast<int>(voff));
        }
        cur_faceColors.insert(cur_faceColors.end(), extra.FaceColors.begin(), extra.FaceColors.end());
        cur_facePairInds.insert(cur_facePairInds.end(), extra_nFaces, pairId);
        
        // Append points for all frames
        for (size_t f = 0; f < nFrames && f < extra.Points3D.size(); ++f) {
            cur_pts3d[f].x.insert(cur_pts3d[f].x.end(), extra.Points3D[f].x.begin(), extra.Points3D[f].x.end());
            cur_pts3d[f].y.insert(cur_pts3d[f].y.end(), extra.Points3D[f].y.begin(), extra.Points3D[f].y.end());
            cur_pts3d[f].z.insert(cur_pts3d[f].z.end(), extra.Points3D[f].z.begin(), extra.Points3D[f].z.end());
            if (f < extra.corrComb.size() && f < cur_corrComb.size()) {
                cur_corrComb[f].insert(cur_corrComb[f].end(), extra.corrComb[f].begin(), extra.corrComb[f].end());
            }
        }
        cur_pointPairInds.insert(cur_pointPairInds.end(), extra_nVerts, pairId);
        
        if (extra.cameraPairInd.size() >= 2) {
            pairIndices[ip * 2] = extra.cameraPairInd[0];
            pairIndices[ip * 2 + 1] = extra.cameraPairInd[1];
        }
        
        std::cout << "  Pair " << pairId << " appended (not stitched): "
                  << extra_nVerts << " points, " << extra_nFaces << " faces" << std::endl;
    }
    
    // ---- Build final DIC3Dcombined ----
    DIC3Dcombined result;
    result.Faces = std::move(cur_faces);
    result.FaceColors = std::move(cur_faceColors);
    result.FacePairInds = std::move(cur_facePairInds);
    result.Points3D = std::move(cur_pts3d);
    result.corrComb = std::move(cur_corrComb);
    result.PointPairInds = std::move(cur_pointPairInds);
    result.pairIndices = std::move(pairIndices);
    
    // Recompute FaceCorrComb, FaceCentroids, Disp from the combined data
    size_t total_faces = result.Faces.size() / 3;
    size_t total_points = result.Points3D.empty() ? 0 : result.Points3D[0].x.size();
    
    result.FaceCorrComb.resize(nFrames);
    result.FaceCentroids.resize(nFrames);
    result.Disp.DispVec.resize(nFrames);
    result.Disp.DispMgn.resize(nFrames);
    
    for (size_t f = 0; f < nFrames; ++f) {
        // Face correlation (max of 3 vertices)
        result.FaceCorrComb[f].resize(total_faces, std::nan(""));
        for (size_t i = 0; i < total_faces; ++i) {
            int v0 = result.Faces[i*3], v1 = result.Faces[i*3+1], v2 = result.Faces[i*3+2];
            if (static_cast<size_t>(v0) < result.corrComb[f].size() &&
                static_cast<size_t>(v1) < result.corrComb[f].size() &&
                static_cast<size_t>(v2) < result.corrComb[f].size()) {
                result.FaceCorrComb[f][i] = std::max({result.corrComb[f][v0], result.corrComb[f][v1], result.corrComb[f][v2]});
            }
        }
        
        // Face centroids (3 doubles per face)
        result.FaceCentroids[f].resize(total_faces * 3, std::nan(""));
        for (size_t i = 0; i < total_faces; ++i) {
            int v0 = result.Faces[i*3], v1 = result.Faces[i*3+1], v2 = result.Faces[i*3+2];
            if (static_cast<size_t>(v0) < total_points && static_cast<size_t>(v1) < total_points && static_cast<size_t>(v2) < total_points) {
                result.FaceCentroids[f][i*3+0] = (result.Points3D[f].x[v0]+result.Points3D[f].x[v1]+result.Points3D[f].x[v2])/3.0;
                result.FaceCentroids[f][i*3+1] = (result.Points3D[f].y[v0]+result.Points3D[f].y[v1]+result.Points3D[f].y[v2])/3.0;
                result.FaceCentroids[f][i*3+2] = (result.Points3D[f].z[v0]+result.Points3D[f].z[v1]+result.Points3D[f].z[v2])/3.0;
            }
        }
        
        // Displacement from frame 0
        result.Disp.DispVec[f].resize(total_points * 3, std::nan(""));
        result.Disp.DispMgn[f].resize(total_points, std::nan(""));
        for (size_t k = 0; k < total_points; ++k) {
            double dx = result.Points3D[f].x[k] - result.Points3D[0].x[k];
            double dy = result.Points3D[f].y[k] - result.Points3D[0].y[k];
            double dz = result.Points3D[f].z[k] - result.Points3D[0].z[k];
            result.Disp.DispVec[f][k*3+0] = dx;
            result.Disp.DispVec[f][k*3+1] = dy;
            result.Disp.DispVec[f][k*3+2] = dz;
            result.Disp.DispMgn[f][k] = std::sqrt(dx*dx + dy*dy + dz*dz);
        }
    }
    
    // Merge calibration and distortion data
    for (const auto& pair : all_pairs) {
        result.calibration.DLT_paths.push_back(pair.DLTpath);
        result.calibration.DLT_params.push_back(pair.DLTparameters);
    }
    for (const auto& pair : all_pairs) {
        result.distortion.distortion_models.push_back(pair.distortionModel);
        result.distortion.distortion_paths.push_back(pair.distortionPath);
    }
    
    std::cout << "\n  ✓ Geometric stitching complete: " 
              << total_points << " points, " << total_faces << " faces" << std::endl;
    
    return result;
}

} // namespace cppxdic
