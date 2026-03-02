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
#include "ncorr.h"
#include <vector>
#include <string>

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
     * Run 3D Reconstruction (Step E) only — for integration testing
     * Requires Step D outputs (.cache/*.bin) and calibration files to exist
     */
    bool runStepE(const std::vector<int>& trial_target) { return dic3DReconstruction(trial_target); }
    
    /**
     * Run Deformation Analysis (Step F) only — for integration testing
     * Requires DIC3Dcombined_*.bin from Step E to exist
     */
    bool runStepF(const std::vector<int>& trial_target) { return dicDeformationAnalysis(trial_target); }
    
private:
    const Config& config_;
    
    // Analysis steps
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
};

#endif // DIC_ANALYSIS_H
