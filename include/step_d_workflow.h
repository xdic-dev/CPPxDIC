/**
 * DIC2D workflow for CPPXDIC
 * Complete implementation of the MATLAB 2D DIC stage
 * Handles the full 2D DIC analysis pipeline
 */

#ifndef STEP_D_WORKFLOW_H
#define STEP_D_WORKFLOW_H

#include "parameters.h"
#include "config.h"
#include <string>
#include <vector>

namespace cppxdic {

/**
 * Dic2DWorkflow class
 * Implements the complete 2D DIC analysis workflow
 * Equivalent to stepD_2DDIC.m in Matlab
 */
class Dic2DWorkflow {
public:
    /**
     * Constructor
     * 
     * @param config Global configuration
     */
    explicit Dic2DWorkflow(const Config& config);
    
    /**
     * Execute the DIC2D workflow
     * Main entry point for 2D DIC processing
     * 
     * @param trial Trial ID string (e.g., "005")
     * @param stereopair Stereo pair number (1 or 2)
     * @return Tuple of (outputPath, pairOrder, pairForced)
     */
    std::tuple<std::string, std::vector<int>, bool> execute(const std::string& trial,
                                                            int stereopair);
    
private:
    const Config& config_;
    BaseParameters base_params_;
    StepParameters step1_params_;      // Tracking camera 1
    StepParameters step2_params_;      // Tracking camera 2
    StepParameters step1_2_params_;    // Matching between cameras
    ProtocolInfo protocol_info_;
    
    /**
     * Setup base parameters for the current trial
     * 
     * @param trial Trial ID
     * @param stereopair Stereo pair number
     * @param reftrial Reference trial ID
     */
    void setupBaseParameters(const std::string& trial, 
                            int stereopair,
                            const std::string& reftrial);
    
    /**
     * Setup step parameters (DIC settings)
     */
    void setupStepParameters();
    
    /**
     * Load protocol information from MAT file
     * 
     * @return Success status
     */
    bool loadProtocol();
    
    /**
     * Determine reference trial based on phase and conditions
     * 
     * @param trial Current trial ID
     * @return Reference trial ID
     */
    std::string determineReferenceTrial(const std::string& trial);
    
    /**
     * Save trial information to MAT file
     * 
     * @param trial Trial ID
     * @param stereopair Stereo pair number
     * @param num_frames Number of frames
     */
    void saveTrialInfo(const std::string& trial, int stereopair, int num_frames);
    
};

using StepDWorkflow = Dic2DWorkflow;

} // namespace cppxdic

#endif // STEP_D_WORKFLOW_H
