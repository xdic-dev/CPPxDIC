#ifndef CPPXDIC_PIPELINE_STEP_D_RUNNER_H
#define CPPXDIC_PIPELINE_STEP_D_RUNNER_H

#include "config.h"
#include <vector>

namespace cppxdic::pipeline {

class Dic2DRunner {
public:
    explicit Dic2DRunner(const Config& config);
    bool run(const std::vector<int>& trial_target) const;

private:
    const Config& config_;
};

using StepDRunner = Dic2DRunner;

} // namespace cppxdic::pipeline

#endif // CPPXDIC_PIPELINE_STEP_D_RUNNER_H
