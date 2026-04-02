#include "cppxdic/pipeline/step_d_runner.h"

#include "dic_analysis.h"

namespace cppxdic::pipeline {

Dic2DRunner::Dic2DRunner(const Config& config)
    : config_(config) {}

bool Dic2DRunner::run(const std::vector<int>& trial_target) const {
    DicAnalysis analysis(config_);
    return analysis.runDic2D(trial_target);
}

} // namespace cppxdic::pipeline
