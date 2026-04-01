#ifndef CPPXDIC_PIPELINE_PIPELINE_RUNNER_H
#define CPPXDIC_PIPELINE_PIPELINE_RUNNER_H

#include "config.h"
#include <ostream>

namespace cppxdic::pipeline {

class PipelineRunner {
public:
    explicit PipelineRunner(const Config& config);

    static void printBanner(std::ostream& os);
    bool run() const;

private:
    const Config& config_;
};

} // namespace cppxdic::pipeline

#endif // CPPXDIC_PIPELINE_PIPELINE_RUNNER_H
