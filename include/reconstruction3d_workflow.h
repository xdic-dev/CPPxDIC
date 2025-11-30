/**
 * 3D Reconstruction Workflow (Step E) for CPPXDIC
 * Handles stereo triangulation and multi-pair stitching
 */

#ifndef RECONSTRUCTION3D_WORKFLOW_H
#define RECONSTRUCTION3D_WORKFLOW_H

#include "config.h"
#include "dic_structures.h"
#include "utils.h"
#include <ncorr.h>
#include <string>
#include <vector>
#include <array>

namespace cppxdic {

/**
 * Input requirements for 3D Reconstruction workflow
 */
struct Reconstruction3DInputs {
    int trial_id;                       // Trial number
    std::string trial_str;              // Trial ID string (e.g., "005")
    int num_pairs;                      // Number of stereo pairs
    
    // Per-pair inputs (indexed by pair-1)
    struct PairInput {
        int cam1;                       // Camera 1 number
        int cam2;                       // Camera 2 number
        std::string dic2d_cache_cam1;   // Path to camera 1 DIC results (.bin)
        std::string dic2d_cache_cam2;   // Path to camera 2 DIC results (.bin)
        std::string calib_cam1;         // Path to camera 1 calibration (.mat)
        std::string calib_cam2;         // Path to camera 2 calibration (.mat)
        std::string distortion_cam1;    // Path to camera 1 distortion params (optional)
        std::string distortion_cam2;    // Path to camera 2 distortion params (optional)
        
        bool isValid() const;
    };
    std::vector<PairInput> pairs;
    
    std::string output_dir;             // Output directory
    
    bool isValid() const;
};

/**
 * Output from 3D Reconstruction workflow
 */
struct Reconstruction3DOutputs {
    std::string combined_binary_path;   // Path to DIC3Dcombined.bin
    std::string combined_mat_path;      // Path to DIC3Dcombined.mat (if generated)
    
    // Statistics
    int num_frames;
    int num_points;
    int num_faces;
    int num_pairs_stitched;
    
    // Per-pair statistics
    struct PairStats {
        int pair_index;
        int num_points;
        int num_faces;
    };
    std::vector<PairStats> pair_stats;
    
    bool isValid() const { return !combined_binary_path.empty(); }
};

/**
 * Reconstruction3DWorkflow class (Step E)
 * 
 * Responsibilities:
 * - Load 2D DIC results from Step D
 * - Load calibration parameters (DLT)
 * - Perform stereo triangulation for each pair
 * - Stitch multiple pairs into combined mesh
 * - Compute displacement fields
 * - Generate DIC3Dcombined output
 * 
 * Inputs (validated in constructor):
 * - Config with valid paths
 * - 2D DIC cache files from Step D
 * - Calibration files for each camera pair
 * 
 * Outputs:
 * - DIC3Dcombined binary file
 * - DIC3Dcombined MAT file (optional)
 */
class Reconstruction3DWorkflow {
public:
    /**
     * Constructor - validates required inputs
     * 
     * @param config Global configuration
     * @throws std::runtime_error if config is invalid
     */
    explicit Reconstruction3DWorkflow(const Config& config);
    
    // =========================================================================
    // Main Entry Point
    // =========================================================================
    
    /**
     * Execute 3D reconstruction for a trial
     * 
     * @param trial_target Vector of trial IDs to process
     * @return true if reconstruction successful for all trials
     */
    bool execute(const std::vector<int>& trial_target);
    
    /**
     * Execute with explicit inputs (for testing)
     * 
     * @param inputs Validated input structure
     * @return Reconstruction3DOutputs with results
     */
    Reconstruction3DOutputs execute(const Reconstruction3DInputs& inputs);
    
    // =========================================================================
    // Milestone Functions (for testing)
    // =========================================================================
    
    /**
     * Load DLT calibration parameters from MAT file
     * 
     * @param calib_path Path to calibration MAT file
     * @return DLT parameters (11 values) or empty vector on failure
     */
    std::vector<double> loadDLTParameters(const std::string& calib_path);
    
    /**
     * Load distortion parameters from MAT file
     * 
     * @param distortion_path Path to distortion MAT file
     * @param params Output: camera parameters
     * @return true if loading successful
     */
    bool loadDistortionParameters(const std::string& distortion_path,
                                  Utils::CameraParameters& params);
    
    /**
     * Build triangular mesh from ROI
     * 
     * @param roi ROI mask from DIC results
     * @param faces Output: face connectivity (3 indices per face)
     * @param index_lut Output: pixel to vertex index lookup
     * @param width Output: ROI width
     * @param height Output: ROI height
     */
    void buildMeshFromROI(const ncorr::ROI2D& roi,
                         std::vector<int>& faces,
                         std::vector<int>& index_lut,
                         int& width, int& height);
    
    /**
     * Extract 2D points from displacement field
     * 
     * @param disp Displacement field
     * @param index_lut Pixel to vertex index lookup
     * @param width ROI width
     * @param height ROI height
     * @return 2D points as flat array [x0,y0,x1,y1,...]
     */
    std::vector<double> extractPoints2D(const ncorr::Disp2D& disp,
                                        const std::vector<int>& index_lut,
                                        int width, int height);
    
    /**
     * Triangulate 3D point from stereo pair
     * 
     * @param L1 DLT parameters for camera 1
     * @param L2 DLT parameters for camera 2
     * @param x1 X coordinate in camera 1
     * @param y1 Y coordinate in camera 1
     * @param x2 X coordinate in camera 2
     * @param y2 Y coordinate in camera 2
     * @return 3D point [X, Y, Z]
     */
    std::array<double, 3> triangulatePoint(const std::vector<double>& L1,
                                           const std::vector<double>& L2,
                                           double x1, double y1,
                                           double x2, double y2);
    
    /**
     * Reconstruct 3D points for a single stereo pair
     * 
     * @param pair_input Input data for this pair
     * @param pair_result Output: 3D reconstruction results
     * @return true if reconstruction successful
     */
    bool reconstructPair(const Reconstruction3DInputs::PairInput& pair_input,
                        DIC3DpairResults& pair_result);
    
    /**
     * Stitch multiple pair results into combined mesh
     * 
     * @param all_pairs Individual pair results
     * @return Combined DIC3Dcombined structure
     */
    DIC3Dcombined stitchPairs(const std::vector<DIC3DpairResults>& all_pairs);
    
    /**
     * Compute displacement from reference frame
     * 
     * @param points3d_ref Reference frame 3D points
     * @param points3d_cur Current frame 3D points
     * @param disp_vec Output: displacement vectors
     * @param disp_mgn Output: displacement magnitudes
     */
    void computeDisplacement(const Points3D& points3d_ref,
                            const Points3D& points3d_cur,
                            std::vector<double>& disp_vec,
                            std::vector<double>& disp_mgn);
    
    /**
     * Compute face centroids
     * 
     * @param points3d 3D points
     * @param faces Face connectivity
     * @return Face centroids as flat array [cx0,cy0,cz0,cx1,cy1,cz1,...]
     */
    std::vector<double> computeFaceCentroids(const Points3D& points3d,
                                             const std::vector<int>& faces);
    
    /**
     * Compute combined correlation (max of both cameras)
     * 
     * @param corr_cam1 Camera 1 correlation coefficients
     * @param corr_cam2 Camera 2 correlation coefficients
     * @return Combined correlation (element-wise max)
     */
    std::vector<double> computeCombinedCorrelation(
        const std::vector<double>& corr_cam1,
        const std::vector<double>& corr_cam2);
    
    // =========================================================================
    // Checkpoint Functions
    // =========================================================================
    
    /**
     * Check if 3D reconstruction output exists
     * 
     * @param output_dir Output directory
     * @param num_pairs Number of pairs
     * @return true if checkpoint exists
     */
    bool hasReconstructionCheckpoint(const std::string& output_dir, int num_pairs);
    
    /**
     * Load existing reconstruction from checkpoint
     * 
     * @param checkpoint_path Path to binary checkpoint
     * @return DIC3Dcombined structure
     */
    DIC3Dcombined loadCheckpoint(const std::string& checkpoint_path);
    
    // =========================================================================
    // Utility Functions
    // =========================================================================
    
    /**
     * Build input structure from config and trial info
     * 
     * @param trial_id Trial number
     * @return Reconstruction3DInputs structure
     */
    Reconstruction3DInputs buildInputs(int trial_id);
    
    /**
     * Find calibration files for a camera pair
     * 
     * @param cam1 Camera 1 number
     * @param cam2 Camera 2 number
     * @param calib_cam1 Output: path to camera 1 calibration
     * @param calib_cam2 Output: path to camera 2 calibration
     * @return true if files found
     */
    bool findCalibrationFiles(int cam1, int cam2,
                             std::string& calib_cam1,
                             std::string& calib_cam2);

private:
    const Config& config_;
    
    /** Apply distortion removal to 2D points */
    void undistortPoints(std::vector<double>& pts,
                        const Utils::CameraParameters& params);
};

} // namespace cppxdic

#endif // RECONSTRUCTION3D_WORKFLOW_H
