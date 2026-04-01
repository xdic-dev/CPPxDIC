#include "cppxdic/pipeline/pipeline_runner.h"

#include "cppxdic/io/project_paths.h"
#include "cppxdic/pipeline/checkpoint_policy.h"
#include "cppxdic/pipeline/step_d_runner.h"
#include "cppxdic/pipeline/step_e_runner.h"
#include "cppxdic/pipeline/step_f_runner.h"
#include "cppxdic/pipeline/trial_selector.h"
#include "data_serializer.h"
#include <chrono>
#include <iostream>

namespace cppxdic::pipeline {

namespace {

void printTrialTargets(const std::vector<int>& trials) {
    std::cout << "Trial target set: [";
    for (size_t index = 0; index < trials.size(); ++index) {
        std::cout << trials[index];
        if (index + 1 < trials.size()) {
            std::cout << ", ";
        }
    }
    std::cout << "]" << std::endl;
}

template <typename Runner>
bool runStep(const char* heading, const char* action, const char* success_message, const Runner& runner,
             const std::vector<int>& trials) {
    std::cout << "\n=== " << heading << " ===" << std::endl;
    std::cout << action << std::endl;
    const auto start = std::chrono::high_resolution_clock::now();
    const bool success = runner.run(trials);
    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    if (!success) {
        return false;
    }
    std::cout << success_message << " in " << duration.count() / 1000.0 << " s" << std::endl;
    return true;
}

} // namespace

PipelineRunner::PipelineRunner(const Config& config)
    : config_(config) {}

void PipelineRunner::printBanner(std::ostream& os) {
    os << "FINGERTIP 3D RECONSTRUCTION using DIC" << std::endl;
    os << "--------------------------------------" << std::endl;
    os << "DIC analysis for the fingertip" << std::endl;
    os << std::endl;
}

bool PipelineRunner::run() const {
    const io::ProjectPaths paths(config_);
    const auto trial_target = TrialSelector(config_, paths).selectTargets();
    printTrialTargets(trial_target);

    const auto serializer = cppxdic::DataSerializer::create(config_.data_format);
    const CheckpointPolicy checkpoints(config_, paths);

    const bool step_d_complete = checkpoints.hasStepDOutputs(trial_target, *serializer);
    if (step_d_complete) {
        std::cout << "\n=== STEP D: 2D-DIC ===" << std::endl;
        std::cout << "✓ Checkpoint detected: All 2D DIC output files exist" << std::endl;
        std::cout << "  Skipping 2D analysis (use existing results)" << std::endl;
    } else if (!runStep("STEP D: 2D-DIC", "Running 2D DIC analysis...", "✓ DIC 2D Analysis done",
                        StepDRunner(config_), trial_target)) {
        std::cerr << "2D DIC Analysis failed!" << std::endl;
        return false;
    }

    const bool step_e_complete = checkpoints.hasStepEOutputs(trial_target, *serializer);
    if (step_e_complete) {
        std::cout << "\n=== STEP E: 3D Reconstruction ===" << std::endl;
        std::cout << "✓ Checkpoint detected: All 3D reconstruction output files exist" << std::endl;
        std::cout << "  Skipping 3D reconstruction (use existing results)" << std::endl;
    } else if (!runStep("STEP E: 3D Reconstruction", "Running 3D reconstruction...", "✓ DIC 3D Reconstruction done",
                        StepERunner(config_), trial_target)) {
        std::cerr << "3D Reconstruction failed!" << std::endl;
        return false;
    }

    const bool step_f_complete = checkpoints.hasStepFOutputs(trial_target, *serializer);
    if (step_f_complete) {
        std::cout << "\n=== STEP F: Deformation Analysis ===" << std::endl;
        std::cout << "✓ Checkpoint detected: Deformation analysis output exists" << std::endl;
        std::cout << "  Skipping deformation analysis (use existing results)" << std::endl;
    } else if (!runStep("STEP F: Deformation Analysis", "Running deformation analysis...", "✓ DIC Deformation Analysis done",
                        StepFRunner(config_), trial_target)) {
        std::cerr << "Deformation Analysis failed!" << std::endl;
        return false;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "✓ All DIC analysis steps complete!" << std::endl;
    std::cout << "========================================" << std::endl;
    return true;
}

} // namespace cppxdic::pipeline
