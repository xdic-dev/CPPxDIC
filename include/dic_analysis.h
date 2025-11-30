/**
 * DIC Analysis class for CPPXDIC
 * Main analysis functionality equivalent to Matlab dic_analysis.m
 * 
 * This class orchestrates the three main DIC analysis steps:
 * - Step D: 2D DIC Analysis (DIC2DWorkflow)
 * - Step E: 3D Reconstruction (Reconstruction3DWorkflow)
 * - Step F: Deformation Analysis (DeformationWorkflow)
 * 
 * Each step is implemented as a separate workflow class with clear
 * inputs, outputs, and testable milestone functions.
 */

#ifndef DIC_ANALYSIS_H
#define DIC_ANALYSIS_H

#include "config.h"
#include "preprocessing_workflow.h"
#include "dic2d_workflow.h"
#include "reconstruction3d_workflow.h"
#include "deformation_workflow.h"
#include "ncorr.h"
#include <vector>
#include <string>
#include <memory>

class DicAnalysis {
public:
    explicit DicAnalysis(const Config& config);
    
    /**
     * Run complete DIC analysis pipeline
     * Executes preprocessing, Step D, Step E, and Step F in sequence
     * 
     * @return true if all steps complete successfully
     */
    bool run();
    
    /**
     * Run with new workflow classes (refactored version)
     * Uses PreprocessingWorkflow, DIC2DWorkflow, Reconstruction3DWorkflow, DeformationWorkflow
     * 
     * @return true if all steps complete successfully
     */
    bool runRefactored();
    
    // =========================================================================
    // Individual Step Access (for testing)
    // =========================================================================
    
    /** Get preprocessing workflow */
    cppxdic::PreprocessingWorkflow& getPreprocessingWorkflow();
    
    /** Get DIC 2D workflow (Step D) */
    cppxdic::DIC2DWorkflow& getDIC2DWorkflow();
    
    /** Get 3D Reconstruction workflow (Step E) */
    cppxdic::Reconstruction3DWorkflow& getReconstruction3DWorkflow();
    
    /** Get Deformation workflow (Step F) */
    cppxdic::DeformationWorkflow& getDeformationWorkflow();
    
private:
    const Config& config_;
    
    // Workflow instances (lazy-initialized)
    std::unique_ptr<cppxdic::PreprocessingWorkflow> preprocessing_;
    std::unique_ptr<cppxdic::DIC2DWorkflow> dic2d_;
    std::unique_ptr<cppxdic::Reconstruction3DWorkflow> reconstruction3d_;
    std::unique_ptr<cppxdic::DeformationWorkflow> deformation_;
    
    // Legacy analysis steps (kept for backward compatibility)
    bool dic2DAnalysis(const std::vector<int>& trial_target);
    bool dic3DReconstruction(const std::vector<int>& trial_target);
    bool dicDeformationAnalysis(const std::vector<int>& trial_target);
    
    // Helper functions
    std::vector<int> searchTrialTarget();
    std::vector<std::string> loadImageSequence(const std::string& trial_path);
    bool setupNcorrAnalysis(const std::vector<std::string>& images, 
                           ncorr::DIC_analysis_input& dic_input);
    bool setupNcorrAnalysis(const std::vector<std::string>& images,
                            const std::string& roi_mask_image_path,
                            ncorr::DIC_analysis_input& dic_input);
    
    // Checkpoint helpers
    bool check2DOutputsExist(const std::vector<int>& trial_target);
    bool check3DOutputsExist(const std::vector<int>& trial_target);
    bool checkDeformationOutputsExist();
};

#endif // DIC_ANALYSIS_H
