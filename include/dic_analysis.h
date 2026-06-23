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
#include "stage_plan.h"
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
     * Run the complete DIC analysis pipeline for an explicit list of trials.
     *
     * Identical to run() but uses the provided trial list instead of the
     * hard-coded/searched default. Enables trial-level experiment drivers
     * (single trial, SLURM arrays) to parameterise a run without touching the
     * default behaviour of run().
     *
     * @param trial_target Non-empty list of 1-based trial IDs to process.
     * @return true if all steps complete successfully
     */
    bool run(const std::vector<int>& trial_target);

    /**
     * Run a selected subset of pipeline stages for an explicit list of trials.
     *
     * Identical to run(trial_target) but only the sub-steps enabled in @p plan
     * are executed; the rest are expected to be satisfied by existing on-disk
     * checkpoints. This is what lets the pipeline be split across independent
     * processes / SLURM array tasks (matching, tracking, E+F).
     *
     * @param trial_target Non-empty list of 1-based trial IDs to process.
     * @param plan         Which sub-steps to run (+ optional pair/camera narrowing).
     * @return true if all requested steps complete successfully
     */
    bool run(const std::vector<int>& trial_target, const StagePlan& plan);

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

    /**
     * Search the protocol .mat for the list of trial IDs of a given subject.
     *
     * This is the per-subject lookup used by run(). It is exposed publicly so
     * tooling (e.g. the gen_subject_trial generator) can enumerate the trials
     * of arbitrary subjects without constructing a full pipeline run.
     *
     * The lookup reuses the calling instance's Config for everything except the
     * subject identity: a copy of config_ is taken, its subject is overridden to
     * @p subject, derived variables are refreshed, and the protocol .mat for that
     * subject is parsed. config_ itself is left untouched.
     *
     * @param subject Subject identifier (e.g. "S09").
     * @return The list of 1-based trial indices for that subject. On failure the
     *         function falls back to {ref_trial_id, ref_trial_id + 1}.
     */
    std::vector<int> searchTrialTarget(const std::string& subject);

private:
    const Config& config_;
    
    // Analysis steps
    bool dic2DAnalysis(const std::vector<int>& trial_target, const StagePlan& plan);
    bool dic3DReconstruction(const std::vector<int>& trial_target);
    bool dicDeformationAnalysis(const std::vector<int>& trial_target);
    
    // Helper functions
    // No-arg overload: looks up trials for config_.subject_id by delegating to
    // searchTrialTarget(const std::string&). Preserves the original behaviour.
    std::vector<int> searchTrialTarget();
    std::vector<std::string> loadImageSequence(const std::string& trial_path);
    bool setupNcorrAnalysis(const std::vector<std::string>& images, 
                           ncorr::DIC_analysis_input& dic_input);
    bool setupNcorrAnalysis(const std::vector<std::string>& images,
                            const std::string& roi_mask_image_path,
                            ncorr::DIC_analysis_input& dic_input);
};

#endif // DIC_ANALYSIS_H
