#include "cppxdic/pipeline/step_e_runner.h"

#include "dic_analysis.h"

namespace cppxdic::pipeline {

StepERunner::StepERunner(const Config& config)
    : config_(config) {}

bool StepERunner::run(const std::vector<int>& trial_target) const {
    DicAnalysis analysis(config_);
    return analysis.runStepE(trial_target);
}

} // namespace cppxdic::pipeline
