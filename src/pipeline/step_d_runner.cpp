#include "cppxdic/pipeline/step_d_runner.h"

#include "dic_analysis.h"

namespace cppxdic::pipeline {

StepDRunner::StepDRunner(const Config& config)
    : config_(config) {}

bool StepDRunner::run(const std::vector<int>& trial_target) const {
    DicAnalysis analysis(config_);
    return analysis.runStepD(trial_target);
}

} // namespace cppxdic::pipeline
