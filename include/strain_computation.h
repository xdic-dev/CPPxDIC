/**
 * Strain Computation for CPPXDIC
 * Port of MATLAB triSurfaceDeformation_rewrited.m
 * Based on Triangular Cosserat Point Elements (TCPE)
 * 
 * Reference:
 * Solav, Dana, et al. "Bone pose estimation in the presence of soft tissue artifact 
 * using triangular cosserat point elements." Annals of biomedical engineering 44.4 (2016): 1181-1190.
 */

#ifndef STRAIN_COMPUTATION_H
#define STRAIN_COMPUTATION_H

#include <Eigen/Dense>
#include <vector>

namespace cppxdic {

/**
 * Deformation result structure
 * Contains all computed deformation and strain measures
 */
struct DeformationResult {
    // Director vectors
    std::vector<Eigen::Vector3d> D1;  // Reference director 1
    std::vector<Eigen::Vector3d> D2;  // Reference director 2
    std::vector<Eigen::Vector3d> D3;  // Reference normal
    std::vector<Eigen::Vector3d> d1;  // Current director 1
    std::vector<Eigen::Vector3d> d2;  // Current director 2
    std::vector<Eigen::Vector3d> d3;  // Current normal
    
    // Reciprocal vectors
    std::vector<Eigen::Vector3d> Drec1;
    std::vector<Eigen::Vector3d> Drec2;
    std::vector<double> Dnorm;
    
    // Deformation gradient tensor (3x3 per face)
    std::vector<Eigen::Matrix3d> Fmat;
    
    // Cauchy-Green tensor
    std::vector<Eigen::Matrix3d> Cmat;
    
    // Principal stretches
    std::vector<double> Lamda1;
    std::vector<double> Lamda2;
    
    // Dilatation (area change)
    std::vector<double> J;
    
    // Lagrangian strain tensor
    std::vector<Eigen::Matrix3d> Emat;
    
    // Eulerian-Almansi strain tensor
    std::vector<Eigen::Matrix3d> emat;
    
    // Strain magnitudes
    std::vector<double> Emgn;  // Lagrangian
    std::vector<double> emgn;  // Eulerian
    
    // Planar principal strains (Lagrangian)
    std::vector<double> Epc1;  // Smallest
    std::vector<double> Epc2;  // Largest
    std::vector<Eigen::Vector3d> Epc1vec;     // Direction (reference)
    std::vector<Eigen::Vector3d> Epc2vec;     // Direction (reference)
    std::vector<Eigen::Vector3d> Epc1vecCur;  // Direction (current)
    std::vector<Eigen::Vector3d> Epc2vecCur;  // Direction (current)
    
    // Planar principal strains (Eulerian)
    std::vector<double> epc1;
    std::vector<double> epc2;
    std::vector<Eigen::Vector3d> epc1vec;
    std::vector<Eigen::Vector3d> epc2vec;
    
    // Maximum shear strain
    std::vector<double> EShearMax;  // Lagrangian
    std::vector<double> eShearMax;  // Eulerian
    std::vector<Eigen::Vector3d> EShearMaxVec1;
    std::vector<Eigen::Vector3d> EShearMaxVec2;
    std::vector<Eigen::Vector3d> EShearMaxVecCur1;
    std::vector<Eigen::Vector3d> EShearMaxVecCur2;
    std::vector<Eigen::Vector3d> eShearMaxVec1;
    std::vector<Eigen::Vector3d> eShearMaxVec2;
    
    // Equivalent strain (von Mises)
    std::vector<double> Eeq;  // Lagrangian
    std::vector<double> eeq;  // Eulerian
    
    // Area
    std::vector<double> Area;
};

/**
 * Frame-based deformation result
 * Contains deformation results for all frames
 */
struct FrameDeformationResult {
    size_t n_frames;
    size_t n_faces;
    
    // Each cell corresponds to one time frame
    std::vector<DeformationResult> frames;
};

/**
 * Compute deformation and strain for triangular surface elements
 * 
 * @param faces Triangle connectivity (Nx3, each row is [v1, v2, v3])
 * @param vertices_ref Reference vertex positions (Mx3)
 * @param vertices_def Deformed vertex positions for each frame (vector of Mx3)
 * @param cumulative If true, use first frame as reference; if false, use previous frame
 * @return Frame-based deformation results
 */
FrameDeformationResult computeTriSurfaceDeformation(
    const std::vector<int>& faces,
    const std::vector<Eigen::Vector3d>& vertices_ref,
    const std::vector<std::vector<Eigen::Vector3d>>& vertices_def,
    bool cumulative = true
);

/**
 * Compute deformation for a single frame
 * 
 * @param faces Triangle connectivity
 * @param vertices_ref Reference vertices
 * @param vertices_cur Current (deformed) vertices
 * @return Deformation result for single frame
 */
DeformationResult computeSingleFrameDeformation(
    const std::vector<int>& faces,
    const std::vector<Eigen::Vector3d>& vertices_ref,
    const std::vector<Eigen::Vector3d>& vertices_cur
);

} // namespace cppxdic

#endif // STRAIN_COMPUTATION_H
