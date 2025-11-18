/**
 * Surface Stitching Implementation for Multi-Pair 3D Reconstruction
 * Based on Matlab's DIC3DsurfaceStitch.m
 */

#include "dic_structures.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>

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

} // namespace cppxdic
