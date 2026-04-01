#include "cppxdic/pipeline/step_f_runner.h"

#include "dic_analysis.h"

namespace cppxdic::pipeline {

StepFRunner::StepFRunner(const Config& config)
    : config_(config) {}

bool StepFRunner::run(const std::vector<int>& trial_target) const {
    DicAnalysis analysis(config_);
    return analysis.runStepF(trial_target);
}

} // namespace cppxdic::pipeline
