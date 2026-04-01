#ifndef CPPXDIC_PIPELINE_STEP_F_RUNNER_H
#define CPPXDIC_PIPELINE_STEP_F_RUNNER_H

#include "config.h"
#include <vector>

namespace cppxdic::pipeline {

class StepFRunner {
public:
    explicit StepFRunner(const Config& config);
    bool run(const std::vector<int>& trial_target) const;

private:
    const Config& config_;
};

} // namespace cppxdic::pipeline

#endif // CPPXDIC_PIPELINE_STEP_F_RUNNER_H
