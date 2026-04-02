#ifndef CPPXDIC_PIPELINE_STEP_F_RUNNER_H
#define CPPXDIC_PIPELINE_STEP_F_RUNNER_H

#include "config.h"
#include <vector>

namespace cppxdic::pipeline {

class DeformationRunner {
public:
    explicit DeformationRunner(const Config& config);
    bool run(const std::vector<int>& trial_target) const;

private:
    const Config& config_;
};

using StepFRunner = DeformationRunner;

} // namespace cppxdic::pipeline

#endif // CPPXDIC_PIPELINE_STEP_F_RUNNER_H
