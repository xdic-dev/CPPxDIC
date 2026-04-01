#ifndef CPPXDIC_PIPELINE_TRIAL_SELECTOR_H
#define CPPXDIC_PIPELINE_TRIAL_SELECTOR_H

#include "config.h"
#include "cppxdic/io/project_paths.h"
#include <vector>

namespace cppxdic::pipeline {

class TrialSelector {
public:
    explicit TrialSelector(const Config& config);
    explicit TrialSelector(const Config& config, const io::ProjectPaths& paths);

    std::vector<int> selectTargets() const;
    static std::vector<int> fallbackTrials(const Config& config);

private:
    const Config& config_;
    const io::ProjectPaths* paths_;
};

} // namespace cppxdic::pipeline

#endif // CPPXDIC_PIPELINE_TRIAL_SELECTOR_H
