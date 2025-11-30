/**
 * Deformation Workflow (Step F) for CPPXDIC
 * Handles strain computation and post-processing
 */

#ifndef DEFORMATION_WORKFLOW_H
#define DEFORMATION_WORKFLOW_H

#include "config.h"
#include "dic_structures.h"
#include "strain_computation.h"
#include "utils.h"
#include <Eigen/Dense>
#include <string>
#include <vector>

namespace cppxdic {

/**
 * Input requirements for Deformation workflow
 */
struct DeformationInputs {
    int trial_id;                       // Trial number
    std::string dic3d_combined_path;    // Path to DIC3Dcombined.bin from Step E
    std::string output_dir;             // Output directory
    
    // Processing options
    bool apply_temporal_filtering;      // Apply temporal smoothing
    double filter_freq;                 // Low-pass cutoff frequency (Hz)
    double acquisition_freq;            // Acquisition frequency (Hz)
    bool cumulative_deformation;        // Use cumulative (vs incremental) deformation
    
    bool isValid() const;
};

/**
 * Output from Deformation workflow
 */
struct DeformationOutputs {
    std::string ppresults_binary_path;  // Path to DIC3DPPresults.bin
    std::string ppresults_mat_path;     // Path to DIC3DPPresults.mat (if generated)
    
    // Statistics
    int num_frames;
    int num_points;
    int num_faces;
    
    // Strain statistics (per frame)
    struct FrameStats {
        double max_principal_strain_1;
        double max_principal_strain_2;
        double max_shear_strain;
        double mean_displacement_magnitude;
    };
    std::vector<FrameStats> frame_stats;
    
    bool isValid() const { return !ppresults_binary_path.empty(); }
};

/**
 * DeformationWorkflow class (Step F)
 * 
 * Responsibilities:
 * - Load DIC3Dcombined from Step E
 * - Apply temporal filtering to displacement fields
 * - Compute rigid body motion (RBM) transformations
 * - Compute deformation gradients and strain tensors
 * - Compute principal strains and max shear
 * - Generate DIC3DPPresults output
 * 
 * Inputs (validated in constructor):
 * - Config with valid paths
 * - DIC3Dcombined binary file from Step E
 * 
 * Outputs:
 * - DIC3DPPresults binary file
 * - DIC3DPPresults MAT file (optional)
 * - Visualization exports (optional)
 */
class DeformationWorkflow {
public:
    /**
     * Constructor - validates required inputs
     * 
     * @param config Global configuration
     * @throws std::runtime_error if config is invalid
     */
    explicit DeformationWorkflow(const Config& config);
    
    // =========================================================================
    // Main Entry Point
    // =========================================================================
    
    /**
     * Execute deformation analysis for trials
     * 
     * @param trial_target Vector of trial IDs to process
     * @return true if analysis successful for all trials
     */
    bool execute(const std::vector<int>& trial_target);
    
    /**
     * Execute with explicit inputs (for testing)
     * 
     * @param inputs Validated input structure
     * @return DeformationOutputs with results
     */
    DeformationOutputs execute(const DeformationInputs& inputs);
    
    // =========================================================================
    // Milestone Functions (for testing)
    // =========================================================================
    
    /**
     * Load DIC3Dcombined from binary file
     * 
     * @param path Path to binary file
     * @return DIC3Dcombined structure
     * @throws std::runtime_error if loading fails
     */
    DIC3Dcombined loadDIC3DCombined(const std::string& path);
    
    /**
     * Convert Points3D to Eigen format
     * 
     * @param points3d Points3D structure
     * @return Vector of Eigen::Vector3d
     */
    std::vector<Eigen::Vector3d> convertToEigen(const Points3D& points3d);
    
    /**
     * Apply temporal filtering to displacement fields
     * 
     * @param vertices_all_frames All frame vertices
     * @param vertices_ref Reference frame vertices
     * @param freq_filter Low-pass cutoff frequency
     * @param freq_acq Acquisition frequency
     * @return Filtered vertices for all frames
     */
    std::vector<std::vector<Eigen::Vector3d>> applyTemporalFiltering(
        const std::vector<std::vector<Eigen::Vector3d>>& vertices_all_frames,
        const std::vector<Eigen::Vector3d>& vertices_ref,
        double freq_filter,
        double freq_acq);
    
    /**
     * Compute rigid body motion transformation
     * 
     * @param vertices_current Current frame vertices
     * @param vertices_reference Reference frame vertices
     * @return Rigid transform (rotation + translation)
     */
    Utils::RigidTransform computeRigidBodyMotion(
        const std::vector<Eigen::Vector3d>& vertices_current,
        const std::vector<Eigen::Vector3d>& vertices_reference);
    
    /**
     * Apply rigid body motion removal
     * 
     * @param vertices_all_frames All frame vertices
     * @param vertices_ref Reference frame vertices
     * @param transforms Output: RBM transforms per frame
     * @return Vertices after RBM removal
     */
    std::vector<std::vector<Eigen::Vector3d>> applyRBMRemoval(
        const std::vector<std::vector<Eigen::Vector3d>>& vertices_all_frames,
        const std::vector<Eigen::Vector3d>& vertices_ref,
        std::vector<Utils::RigidTransform>& transforms);
    
    /**
     * Compute surface deformation using TCPE method
     * 
     * @param faces Face connectivity
     * @param vertices_ref Reference vertices
     * @param vertices_all_frames All frame vertices
     * @param cumulative Use cumulative deformation
     * @return Frame deformation results
     */
    FrameDeformationResult computeSurfaceDeformation(
        const std::vector<int>& faces,
        const std::vector<Eigen::Vector3d>& vertices_ref,
        const std::vector<std::vector<Eigen::Vector3d>>& vertices_all_frames,
        bool cumulative);
    
    /**
     * Recompute displacement after filtering
     * 
     * @param vertices_all_frames Filtered vertices
     * @param vertices_ref Reference vertices
     * @param disp Output: displacement data
     */
    void recomputeDisplacement(
        const std::vector<std::vector<Eigen::Vector3d>>& vertices_all_frames,
        const std::vector<Eigen::Vector3d>& vertices_ref,
        DispData& disp);
    
    /**
     * Recompute face centroids after filtering
     * 
     * @param vertices_all_frames Filtered vertices
     * @param faces Face connectivity
     * @return Face centroids per frame
     */
    std::vector<std::vector<double>> recomputeFaceCentroids(
        const std::vector<std::vector<Eigen::Vector3d>>& vertices_all_frames,
        const std::vector<int>& faces);
    
    /**
     * Recompute face correlation after filtering
     * 
     * @param point_corr Point correlation per frame
     * @param faces Face connectivity
     * @return Face correlation per frame
     */
    std::vector<std::vector<double>> recomputeFaceCorrelation(
        const std::vector<std::vector<double>>& point_corr,
        const std::vector<int>& faces);
    
    /**
     * Compute face isotropy index
     * 
     * @param faces Face connectivity
     * @param vertices Vertices for this frame
     * @return Isotropy index per face
     */
    std::vector<double> computeFaceIsotropyIndex(
        const std::vector<int>& faces,
        const std::vector<Eigen::Vector3d>& vertices);
    
    /**
     * Build DIC3DPPresults structure
     * 
     * @param dic3d Input DIC3Dcombined
     * @param deform_result Deformation results (with RBM)
     * @param deform_result_arbm Deformation results (after RBM)
     * @param rbm_transforms RBM transforms
     * @param vertices_arbm Vertices after RBM
     * @param face_iso_ind Face isotropy indices
     * @return DIC3DPPresults structure
     */
    DIC3DPPresults buildPPResults(
        const DIC3Dcombined& dic3d,
        const FrameDeformationResult& deform_result,
        const FrameDeformationResult& deform_result_arbm,
        const std::vector<Utils::RigidTransform>& rbm_transforms,
        const std::vector<std::vector<Eigen::Vector3d>>& vertices_arbm,
        const std::vector<std::vector<double>>& face_iso_ind);
    
    /**
     * Convert FrameDeformationResult to DeformData
     * 
     * @param result Frame deformation result
     * @param num_faces Number of faces
     * @return DeformData structure
     */
    DeformData convertToDeformData(const FrameDeformationResult& result,
                                   size_t num_faces);
    
    // =========================================================================
    // Checkpoint Functions
    // =========================================================================
    
    /**
     * Check if deformation output exists
     * 
     * @param output_dir Output directory
     * @param num_pairs Number of pairs
     * @return true if checkpoint exists
     */
    bool hasDeformationCheckpoint(const std::string& output_dir, int num_pairs);
    
    /**
     * Load existing results from checkpoint
     * 
     * @param checkpoint_path Path to binary checkpoint
     * @return DIC3DPPresults structure
     */
    DIC3DPPresults loadCheckpoint(const std::string& checkpoint_path);
    
    // =========================================================================
    // Output Functions
    // =========================================================================
    
    /**
     * Save results to binary file
     * 
     * @param ppresults Results to save
     * @param path Output path
     * @return true if save successful
     */
    bool saveBinary(const DIC3DPPresults& ppresults, const std::string& path);
    
    /**
     * Save results to MAT file
     * 
     * @param ppresults Results to save
     * @param path Output path
     * @return true if save successful
     */
    bool saveMAT(const DIC3DPPresults& ppresults, const std::string& path);
    
    /**
     * Generate visualization exports
     * 
     * @param ppresults Results to visualize
     * @param output_dir Output directory
     * @param trial Trial ID
     * @return true if export successful
     */
    bool generateVisualization(const DIC3DPPresults& ppresults,
                              const std::string& output_dir,
                              int trial);
    
    // =========================================================================
    // Utility Functions
    // =========================================================================
    
    /**
     * Build input structure from config and trial info
     * 
     * @param trial_id Trial number
     * @return DeformationInputs structure
     */
    DeformationInputs buildInputs(int trial_id);
    
    /**
     * Update Points3D from Eigen vertices
     * 
     * @param vertices_all_frames Eigen vertices
     * @param points3d Output: Points3D structures
     */
    void updatePoints3D(
        const std::vector<std::vector<Eigen::Vector3d>>& vertices_all_frames,
        std::vector<Points3D>& points3d);

private:
    const Config& config_;
};

} // namespace cppxdic

#endif // DEFORMATION_WORKFLOW_H
