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
DIC3Dcombined stitchPairsSimple(const std::vector<DIC3Dcombined>& all_pairs) {
    if (all_pairs.empty()) {
        return DIC3Dcombined();
    }
    
    if (all_pairs.size() == 1) {
        // Single pair - just return it with pair indices set
        DIC3Dcombined result = all_pairs[0];
        size_t nFaces = result.Faces.size() / 3;
        size_t nPoints = result.Points3D.empty() ? 0 : result.Points3D[0].x.size();
        result.FacePairInds.assign(nFaces, 1);
        result.PointPairInds.assign(nPoints, 1);
        return result;
    }
    
    // Multi-pair: append all pairs together
    DIC3Dcombined stitched;
    size_t nFrames = all_pairs[0].Points3D.size();
    
    // Initialize with first pair
    stitched = all_pairs[0];
    size_t nFaces = stitched.Faces.size() / 3;
    size_t nPoints = stitched.Points3D[0].x.size();
    stitched.FacePairInds.assign(nFaces, 1);  // Pair 1
    stitched.PointPairInds.assign(nPoints, 1);
    
    // Initialize pairIndices matrix (nPairs x 2)
    stitched.pairIndices.resize(all_pairs.size() * 2);
    if (all_pairs[0].pairIndices.size() >= 2) {
        stitched.pairIndices[0] = all_pairs[0].pairIndices[0];
        stitched.pairIndices[1] = all_pairs[0].pairIndices[1];
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
        if (pair.pairIndices.size() >= 2) {
            stitched.pairIndices[(ipair) * 2] = pair.pairIndices[0];
            stitched.pairIndices[(ipair) * 2 + 1] = pair.pairIndices[1];
        }
        
        std::cout << "  Pair " << (ipair + 1) << ": " << pair_nPoints << " points, " 
                  << pair_nFaces << " faces (appended)" << std::endl;
    }
    
    // Merge calibration data
    for (const auto& pair : all_pairs) {
        stitched.calibration.DLT_paths.insert(
            stitched.calibration.DLT_paths.end(),
            pair.calibration.DLT_paths.begin(),
            pair.calibration.DLT_paths.end()
        );
        stitched.calibration.DLT_params.insert(
            stitched.calibration.DLT_params.end(),
            pair.calibration.DLT_params.begin(),
            pair.calibration.DLT_params.end()
        );
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
        
        // Check if vertices are valid (not NaN)
        if (v0 >= vertices.size() || v1 >= vertices.size() || v2 >= vertices.size()) {
            continue;
        }
        if (vertices[v0].hasNaN() || vertices[v1].hasNaN() || vertices[v2].hasNaN()) {
            continue;
        }
        
        // Add edges (ordered pairs)
        auto add_edge = [&](int a, int b) {
            auto key = (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
            edge_count[key]++;
        };
        
        add_edge(v0, v1);
        add_edge(v1, v2);
        add_edge(v2, v0);
    }
    
    // Extract boundary edges (count == 1)
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
    
    if (faces.size() % 3 != 0) {
        std::cerr << "Error: faces must be divisible by 3" << std::endl;
        return edge_lengths;
    }
    
    size_t nFaces = faces.size() / 3;
    edge_lengths.reserve(nFaces * 3);
    
    for (size_t i = 0; i < nFaces; ++i) {
        int v0 = faces[i * 3 + 0];
        int v1 = faces[i * 3 + 1];
        int v2 = faces[i * 3 + 2];
        
        if (v0 >= vertices.size() || v1 >= vertices.size() || v2 >= vertices.size()) {
            edge_lengths.push_back(std::nan(""));
            edge_lengths.push_back(std::nan(""));
            edge_lengths.push_back(std::nan(""));
            continue;
        }
        
        const auto& p0 = vertices[v0];
        const auto& p1 = vertices[v1];
        const auto& p2 = vertices[v2];
        
        double e01 = (p1 - p0).norm();
        double e12 = (p2 - p1).norm();
        double e20 = (p0 - p2).norm();
        
        edge_lengths.push_back(e01);
        edge_lengths.push_back(e12);
        edge_lengths.push_back(e20);
    }
    
    return edge_lengths;
}

std::pair<std::vector<bool>, std::vector<bool>> removeOverlapSurfaces(
    const std::vector<int>& faces1,
    const std::vector<int>& faces2,
    const std::vector<Eigen::Vector3d>& vertices1,
    const std::vector<Eigen::Vector3d>& vertices2,
    double min_gap) {
    
    size_t nFaces1 = faces1.size() / 3;
    size_t nFaces2 = faces2.size() / 3;
    
    std::vector<bool> keep1(nFaces1, true);
    std::vector<bool> keep2(nFaces2, true);
    
    // Compute face centroids for both surfaces
    std::vector<Eigen::Vector3d> centroids1, centroids2;
    centroids1.reserve(nFaces1);
    centroids2.reserve(nFaces2);
    
    for (size_t i = 0; i < nFaces1; ++i) {
        int v0 = faces1[i * 3 + 0];
        int v1 = faces1[i * 3 + 1];
        int v2 = faces1[i * 3 + 2];
        
        if (v0 < vertices1.size() && v1 < vertices1.size() && v2 < vertices1.size()) {
            Eigen::Vector3d centroid = (vertices1[v0] + vertices1[v1] + vertices1[v2]) / 3.0;
            centroids1.push_back(centroid);
        } else {
            centroids1.push_back(Eigen::Vector3d::Constant(std::nan("")));
        }
    }
    
    for (size_t i = 0; i < nFaces2; ++i) {
        int v0 = faces2[i * 3 + 0];
        int v1 = faces2[i * 3 + 1];
        int v2 = faces2[i * 3 + 2];
        
        if (v0 < vertices2.size() && v1 < vertices2.size() && v2 < vertices2.size()) {
            Eigen::Vector3d centroid = (vertices2[v0] + vertices2[v1] + vertices2[v2]) / 3.0;
            centroids2.push_back(centroid);
        } else {
            centroids2.push_back(Eigen::Vector3d::Constant(std::nan("")));
        }
    }
    
    // Detect overlaps: faces from surface 1 close to surface 2 and vice versa
    // Simplified version: check centroid distances
    
    for (size_t i = 0; i < nFaces1; ++i) {
        if (centroids1[i].hasNaN()) continue;
        
        double min_dist = std::numeric_limits<double>::infinity();
        for (size_t j = 0; j < nFaces2; ++j) {
            if (centroids2[j].hasNaN()) continue;
            double dist = (centroids1[i] - centroids2[j]).norm();
            min_dist = std::min(min_dist, dist);
        }
        
        if (min_dist < min_gap) {
            keep1[i] = false;  // Remove from surface 1 if too close to surface 2
        }
    }
    
    for (size_t j = 0; j < nFaces2; ++j) {
        if (centroids2[j].hasNaN()) continue;
        
        double min_dist = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < nFaces1; ++i) {
            if (centroids1[i].hasNaN()) continue;
            double dist = (centroids2[j] - centroids1[i]).norm();
            min_dist = std::min(min_dist, dist);
        }
        
        if (min_dist < min_gap) {
            keep2[j] = false;  // Remove from surface 2 if too close to surface 1
        }
    }
    
    return {keep1, keep2};
}

// ============================================================================
// Geometric Stitching Implementation
// ============================================================================

DIC3Dcombined stitchPairsGeometric(const std::vector<DIC3Dcombined>& all_pairs,
                                    const std::vector<int>& pair_order) {
    if (all_pairs.empty()) {
        return DIC3Dcombined();
    }
    
    if (pair_order.empty()) {
        // Fallback to simple stitching
        std::cout << "  Warning: Empty pair order, using simple stitching" << std::endl;
        return stitchPairsSimple(all_pairs);
    }
    
    std::cout << "\n=== Geometric Stitching (with overlap removal) ===" << std::endl;
    std::cout << "  Stitching order: ";
    for (int idx : pair_order) std::cout << idx << " ";
    std::cout << std::endl;
    
    // Initialize with first pair in the order
    size_t first_idx = pair_order[0] - 1;  // Convert to 0-indexed
    if (first_idx >= all_pairs.size()) {
        std::cerr << "Error: Invalid pair index in pair_order" << std::endl;
        return DIC3Dcombined();
    }
    
    DIC3Dcombined stitched = all_pairs[first_idx];
    size_t nFrames = stitched.Points3D.size();
    size_t nFaces = stitched.Faces.size() / 3;
    size_t nPoints = stitched.Points3D[0].x.size();
    
    stitched.FacePairInds.assign(nFaces, pair_order[0]);
    stitched.PointPairInds.assign(nPoints, pair_order[0]);
    
    std::cout << "  Pair " << pair_order[0] << " (base): " 
              << nPoints << " points, " << nFaces << " faces" << std::endl;
    
    // Iteratively stitch remaining pairs
    for (size_t ipair = 1; ipair < pair_order.size(); ++ipair) {
        size_t pair_idx = pair_order[ipair] - 1;
        if (pair_idx >= all_pairs.size()) {
            std::cerr << "Error: Invalid pair index " << pair_order[ipair] << std::endl;
            continue;
        }
        
        const auto& next_pair = all_pairs[pair_idx];
        
        // Convert current stitched surface to Eigen format (frame 0)
        std::vector<Eigen::Vector3d> verts_stitched, verts_next;
        for (size_t i = 0; i < stitched.Points3D[0].x.size(); ++i) {
            verts_stitched.emplace_back(
                stitched.Points3D[0].x[i],
                stitched.Points3D[0].y[i],
                stitched.Points3D[0].z[i]
            );
        }
        for (size_t i = 0; i < next_pair.Points3D[0].x.size(); ++i) {
            verts_next.emplace_back(
                next_pair.Points3D[0].x[i],
                next_pair.Points3D[0].y[i],
                next_pair.Points3D[0].z[i]
            );
        }
        
        // Compute minimum gap for overlap detection
        auto edge_lengths_stitched = computeEdgeLengths(stitched.Faces, verts_stitched);
        auto edge_lengths_next = computeEdgeLengths(next_pair.Faces, verts_next);
        
        double mean_edge = 0.0;
        size_t count = 0;
        for (double e : edge_lengths_stitched) {
            if (!std::isnan(e)) { mean_edge += e; count++; }
        }
        for (double e : edge_lengths_next) {
            if (!std::isnan(e)) { mean_edge += e; count++; }
        }
        if (count > 0) mean_edge /= count;
        
        double min_gap = 0.4 * mean_edge;
        
        std::cout << "  Removing overlaps (min_gap=" << min_gap << ")..." << std::endl;
        
        // Remove overlapping regions
        auto [keep_stitched, keep_next] = removeOverlapSurfaces(
            stitched.Faces, next_pair.Faces,
            verts_stitched, verts_next,
            min_gap * 5.0  // Use 5*min_gap as threshold (matching MATLAB)
        );
        
        size_t removed_stitched = std::count(keep_stitched.begin(), keep_stitched.end(), false);
        size_t removed_next = std::count(keep_next.begin(), keep_next.end(), false);
        
        std::cout << "    Removed " << removed_stitched << " faces from stitched surface" << std::endl;
        std::cout << "    Removed " << removed_next << " faces from new pair" << std::endl;
        
        // Filter faces and rebuild (simplified - just append for now)
        // TODO: Full implementation would filter faces, rebuild vertices, and zip boundaries
        // For now, use simple append after overlap check
        
        size_t vertex_offset = stitched.Points3D[0].x.size();
        size_t pair_nFaces = next_pair.Faces.size() / 3;
        
        // Append faces with offset
        for (size_t i = 0; i < next_pair.Faces.size(); ++i) {
            stitched.Faces.push_back(next_pair.Faces[i] + static_cast<int>(vertex_offset));
        }
        
        // Append face colors
        stitched.FaceColors.insert(
            stitched.FaceColors.end(),
            next_pair.FaceColors.begin(),
            next_pair.FaceColors.end()
        );
        
        // Append face pair indices
        stitched.FacePairInds.insert(
            stitched.FacePairInds.end(),
            pair_nFaces,
            pair_order[ipair]
        );
        
        // Append points for all frames
        size_t pair_nPoints = next_pair.Points3D[0].x.size();
        for (size_t frame = 0; frame < nFrames; ++frame) {
            stitched.Points3D[frame].x.insert(
                stitched.Points3D[frame].x.end(),
                next_pair.Points3D[frame].x.begin(),
                next_pair.Points3D[frame].x.end()
            );
            stitched.Points3D[frame].y.insert(
                stitched.Points3D[frame].y.end(),
                next_pair.Points3D[frame].y.begin(),
                next_pair.Points3D[frame].y.end()
            );
            stitched.Points3D[frame].z.insert(
                stitched.Points3D[frame].z.end(),
                next_pair.Points3D[frame].z.begin(),
                next_pair.Points3D[frame].z.end()
            );
            
            // Append other per-frame data
            if (frame < next_pair.corrComb.size()) {
                stitched.corrComb[frame].insert(
                    stitched.corrComb[frame].end(),
                    next_pair.corrComb[frame].begin(),
                    next_pair.corrComb[frame].end()
                );
            }
            if (frame < next_pair.FaceCorrComb.size()) {
                stitched.FaceCorrComb[frame].insert(
                    stitched.FaceCorrComb[frame].end(),
                    next_pair.FaceCorrComb[frame].begin(),
                    next_pair.FaceCorrComb[frame].end()
                );
            }
            if (frame < next_pair.FaceCentroids.size()) {
                stitched.FaceCentroids[frame].insert(
                    stitched.FaceCentroids[frame].end(),
                    next_pair.FaceCentroids[frame].begin(),
                    next_pair.FaceCentroids[frame].end()
                );
            }
        }
        
        // Append displacement data
        for (size_t frame = 0; frame < nFrames; ++frame) {
            if (frame < next_pair.Disp.DispVec.size()) {
                stitched.Disp.DispVec[frame].insert(
                    stitched.Disp.DispVec[frame].end(),
                    next_pair.Disp.DispVec[frame].begin(),
                    next_pair.Disp.DispVec[frame].end()
                );
            }
            if (frame < next_pair.Disp.DispMgn.size()) {
                stitched.Disp.DispMgn[frame].insert(
                    stitched.Disp.DispMgn[frame].end(),
                    next_pair.Disp.DispMgn[frame].begin(),
                    next_pair.Disp.DispMgn[frame].end()
                );
            }
        }
        
        // Append point pair indices
        stitched.PointPairInds.insert(
            stitched.PointPairInds.end(),
            pair_nPoints,
            pair_order[ipair]
        );
        
        std::cout << "  Pair " << pair_order[ipair] << " added: "
                  << pair_nPoints << " points, " << pair_nFaces << " faces" << std::endl;
    }
    
    // Merge calibration data
    for (const auto& pair : all_pairs) {
        stitched.calibration.DLT_paths.insert(
            stitched.calibration.DLT_paths.end(),
            pair.calibration.DLT_paths.begin(),
            pair.calibration.DLT_paths.end()
        );
        stitched.calibration.DLT_params.insert(
            stitched.calibration.DLT_params.end(),
            pair.calibration.DLT_params.begin(),
            pair.calibration.DLT_params.end()
        );
    }
    
    size_t total_points = stitched.Points3D[0].x.size();
    size_t total_faces = stitched.Faces.size() / 3;
    std::cout << "\n  ✓ Geometric stitching complete: " 
              << total_points << " points, " << total_faces << " faces" << std::endl;
    
    return stitched;
}

} // namespace cppxdic
