/**
 * Data structures for DIC results
 * Matches MATLAB xDIC structure format
 */

#ifndef DIC_STRUCTURES_H
#define DIC_STRUCTURES_H

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <map>

namespace cppxdic {

/**
 * DIC Info structure (parameters)
 * Matches MATLAB ncorrInfo structure
 */
struct DICInfo {
    std::vector<double> cutoff_corrcoef;  // Array of cutoff values (301 elements)
    double cutoff_diffnorm;
    int cutoff_iteration;
    std::vector<std::string> imgcorr;  // Cell array of image names (length 2)
    int lenscoef;
    double pixtounits;  // Pixels to units conversion
    int radius;
    int spacing;
    // stepanalysis struct (placeholder - can be expanded if needed)
    bool subsettrunc;
    int total_threads;
    std::string type;
    std::string units;
};

/**
 * 2D Points per frame
 */
struct Points2D {
    std::vector<double> x;  // X coordinates
    std::vector<double> y;  // Y coordinates
};

/**
 * 3D Points per frame
 */
struct Points3D {
    std::vector<double> x;  // X coordinates
    std::vector<double> y;  // Y coordinates
    std::vector<double> z;  // Z coordinates
};

/**
 * Displacement data
 */
struct DispData {
    std::vector<std::vector<double>> DispVec;  // Displacement vectors per frame
    std::vector<std::vector<double>> DispMgn;  // Displacement magnitudes per frame
};

/**
 * Calibration data
 */
struct CalibrationData {
    // 2x2 arrays for stereo pairs (rows=cameras, cols=pairs)
    std::vector<std::vector<std::string>> DLT_paths;  // 2x2 cell array of paths
    std::vector<std::vector<std::vector<double>>> DLT_params;  // 2x2 cell array of parameter vectors
};

/**
 * DIC 2D Pair Results
 * Equivalent to MATLAB's DIC2DpairResults
 */
struct DIC2DPairResults {
    int nCamRef;        // Reference camera number
    int nCamDef;        // Deformed camera number
    int nImages;        // Number of images
    cv::Mat ROImask;    // ROI mask
    DICInfo ncorrInfo;  // DIC parameters
    std::vector<Points2D> Points;  // Points per frame (cell array in MATLAB: each cell is Nx2 array)
    std::vector<std::vector<double>> CorCoeffVec;  // Correlation coefficients per frame (cell array: each cell is Nx1 array)
    std::vector<int> Faces;  // Triangular mesh faces (Nx3 flattened)
    std::vector<double> FaceColors;  // Face color values
};

/**
 * Distortion data
 */
struct DistortionData {
    // 2x2 arrays for stereo pairs (rows=cameras, cols=pairs)
    std::vector<std::vector<std::string>> distortion_models;  // 2x2 cell array of model names
    std::vector<std::vector<std::string>> distortion_paths;   // 2x2 cell array of paths
};

/**
 * DIC 3D Combined Results
 * Equivalent to MATLAB's DIC3Dcombined
 */
struct DIC3Dcombined {
    std::vector<int> pairIndices;  // Camera pair indices
    std::vector<Points3D> Points3D;  // 3D points per frame (cell array in MATLAB)
    std::vector<int> Faces;  // Triangular mesh faces (Nx3 flattened)
    std::vector<double> FaceColors;  // Face colors
    std::vector<std::vector<double>> corrComb;  // Combined correlation coefficients per frame
    std::vector<std::vector<double>> FaceCorrComb;  // Face correlation coefficients per frame
    std::vector<std::vector<double>> FaceCentroids;  // Triangle centroids per frame (nFrames x nFaces x 3)
    DispData Disp;  // Displacement data
    CalibrationData calibration;  // Calibration info
    DistortionData distortion;  // Distortion model and paths
    
    // Multi-pair stitching metadata
    std::vector<int> FacePairInds;  // Which stereo pair each face belongs to (1-indexed)
    std::vector<int> PointPairInds; // Which stereo pair each point belongs to (1-indexed)
    
    // Original DIC 2D info
    std::vector<DIC2DPairResults> DIC2Dinfo;
    
    // Individual pair results (for multiple pairs)
    std::vector<DIC3Dcombined> AllPairsResults;
    
    // Binary serialization methods
    void saveBinary(const std::string& filepath) const;
    static DIC3Dcombined loadBinary(const std::string& filepath);
};

/**
 * Deformation gradient tensor
 */
struct DeformGradient {
    std::vector<double> F11, F12, F13;
    std::vector<double> F21, F22, F23;
    std::vector<double> F31, F32, F33;
};

/**
 * Strain tensor
 */
struct StrainTensor {
    std::vector<double> E11, E12, E13;
    std::vector<double> E21, E22, E23;
    std::vector<double> E31, E32, E33;
};

/**
 * Deformation data
 */
struct DeformData {
    std::vector<DeformGradient> F;  // Deformation gradients per frame
    std::vector<StrainTensor> strain;  // Strain tensors per frame (Green-Lagrange)
    std::vector<std::vector<double>> princStrain;  // Principal strains per frame
    std::vector<std::vector<double>> maxShearStrain;  // Max shear strain per frame
};

/**
 * Rigid Body Motion (RBM) data
 */
struct RBMData {
    std::vector<std::vector<double>> RotMat;   // Rotation matrices per frame (9 values each)
    std::vector<std::vector<double>> TransVec; // Translation vectors per frame (3 values each)
};

/**
 * DIC 3D Post-Processing Results
 * Equivalent to MATLAB's DIC3DPPresults
 */
struct DIC3DPPresults : DIC3Dcombined {
    DeformData Deform;         // Deformation and strain data (with RBM)
    DeformData Deform_ARBM;    // Deformation and strain data after RBM removal
    RBMData RBM;               // Rigid body motion transformation data
    std::vector<std::vector<double>> Points3D_ARBM_x;  // 3D points after RBM per frame
    std::vector<std::vector<double>> Points3D_ARBM_y;
    std::vector<std::vector<double>> Points3D_ARBM_z;
    std::vector<std::vector<double>> FaceIsoInd;  // Face isotropy index per frame
    std::string deftype;  // "cum" (cumulative) or "rate"
    size_t n_frames;  // Number of frames processed
};

} // namespace cppxdic

#endif // DIC_STRUCTURES_H
