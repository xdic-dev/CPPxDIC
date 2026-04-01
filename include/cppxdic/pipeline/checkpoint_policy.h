#ifndef CPPXDIC_PIPELINE_CHECKPOINT_POLICY_H
#define CPPXDIC_PIPELINE_CHECKPOINT_POLICY_H

#include "config.h"
#include "cppxdic/io/project_paths.h"
#include "data_serializer.h"
#include <vector>

namespace cppxdic::pipeline {

class CheckpointPolicy {
public:
    CheckpointPolicy(const Config& config, const io::ProjectPaths& paths);

    bool hasStepDOutputs(const std::vector<int>& trials, const cppxdic::DataSerializer& serializer) const;
    bool hasStepEOutputs(const std::vector<int>& trials, const cppxdic::DataSerializer& serializer) const;
    bool hasStepFOutputs(const std::vector<int>& trials, const cppxdic::DataSerializer& serializer) const;

private:
    const Config& config_;
    const io::ProjectPaths& paths_;
};

} // namespace cppxdic::pipeline

#endif // CPPXDIC_PIPELINE_CHECKPOINT_POLICY_H
