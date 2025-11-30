/**
 * Preprocessing Workflow for CPPXDIC
 * Handles trial discovery, protocol loading, and data preparation
 * before the main DIC analysis steps
 */

#ifndef PREPROCESSING_WORKFLOW_H
#define PREPROCESSING_WORKFLOW_H

#include "config.h"
#include <string>
#include <vector>
#include <optional>

namespace cppxdic {

/**
 * Protocol information loaded from MAT file
 */
struct ProtocolData {
    std::vector<std::string> dircond;   // Direction conditions
    std::vector<int> nfcond;            // Number of conditions
    std::vector<double> spdcond;        // Speed conditions
    std::vector<int> repcond;           // Repetition conditions
    
    bool isValid() const { return !dircond.empty(); }
};

/**
 * Trial information for processing
 */
struct TrialInfo {
    int trial_id;
    std::string trial_str;              // Zero-padded string (e.g., "005")
    std::string direction;              // Direction condition
    int nf;                             // Number of condition
    double speed;                       // Speed condition
    std::string reference_trial;        // Reference trial for this trial
};

/**
 * PreprocessingWorkflow class
 * 
 * Responsibilities:
 * - Load and parse protocol files
 * - Discover target trials based on configuration
 * - Validate input data paths and files
 * - Prepare trial information for downstream workflows
 * 
 * Inputs (validated in constructor):
 * - Config with valid data_path and dic_path
 * - Protocol MAT file in expected location
 * 
 * Outputs:
 * - List of target trials to process
 * - Protocol data for trial metadata
 * - Validated paths for each trial
 */
class PreprocessingWorkflow {
public:
    /**
     * Constructor - validates required inputs
     * 
     * @param config Global configuration (must have valid paths)
     * @throws std::runtime_error if required paths don't exist
     */
    explicit PreprocessingWorkflow(const Config& config);
    
    // =========================================================================
    // Main Entry Point
    // =========================================================================
    
    /**
     * Execute preprocessing workflow
     * Discovers trials and prepares data for analysis
     * 
     * @return true if preprocessing successful
     */
    bool execute();
    
    // =========================================================================
    // Milestone Functions (for testing)
    // =========================================================================
    
    /**
     * Load protocol from MAT file
     * 
     * @return Protocol data or nullopt if loading fails
     */
    std::optional<ProtocolData> loadProtocol();
    
    /**
     * Search for target trials based on config filters
     * 
     * @param protocol Protocol data to filter against
     * @return Vector of trial IDs to process
     */
    std::vector<int> searchTrialTargets(const ProtocolData& protocol);
    
    /**
     * Validate that all required files exist for a trial
     * 
     * @param trial_id Trial number
     * @param stereopair Stereo pair number
     * @return true if all required files exist
     */
    bool validateTrialInputs(int trial_id, int stereopair);
    
    /**
     * Get trial information for a specific trial
     * 
     * @param trial_id Trial number
     * @param protocol Protocol data
     * @return TrialInfo structure
     */
    TrialInfo getTrialInfo(int trial_id, const ProtocolData& protocol);
    
    /**
     * Determine reference trial for a given trial
     * 
     * @param trial_id Current trial
     * @param protocol Protocol data
     * @return Reference trial ID string (zero-padded)
     */
    std::string determineReferenceTrial(int trial_id, const ProtocolData& protocol);
    
    // =========================================================================
    // Accessors for Results
    // =========================================================================
    
    /** Get discovered target trials */
    const std::vector<int>& getTargetTrials() const { return target_trials_; }
    
    /** Get loaded protocol data */
    const ProtocolData& getProtocolData() const { return protocol_; }
    
    /** Check if preprocessing completed successfully */
    bool isReady() const { return is_ready_; }
    
    // =========================================================================
    // Path Helpers
    // =========================================================================
    
    /** Get protocol directory path */
    std::string getProtocolPath() const;
    
    /** Get calibration directory path */
    std::string getCalibrationPath() const;
    
    /** Get output directory for a trial */
    std::string getTrialOutputPath(int trial_id, const std::string& phase) const;
    
    /** Get video frames directory for a trial */
    std::string getTrialVideoPath(int trial_id, int stereopair) const;

private:
    const Config& config_;
    ProtocolData protocol_;
    std::vector<int> target_trials_;
    bool is_ready_ = false;
    
    /** Validate configuration paths exist */
    void validatePaths();
};

} // namespace cppxdic

#endif // PREPROCESSING_WORKFLOW_H
