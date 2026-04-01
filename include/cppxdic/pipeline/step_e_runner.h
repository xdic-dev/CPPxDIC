#ifndef CPPXDIC_PIPELINE_STEP_E_RUNNER_H
#define CPPXDIC_PIPELINE_STEP_E_RUNNER_H

#include "config.h"
#include <vector>

namespace cppxdic::pipeline {

class StepERunner {
public:
    explicit StepERunner(const Config& config);
    bool run(const std::vector<int>& trial_target) const;

private:
    const Config& config_;
};

} // namespace cppxdic::pipeline

#endif // CPPXDIC_PIPELINE_STEP_E_RUNNER_H
