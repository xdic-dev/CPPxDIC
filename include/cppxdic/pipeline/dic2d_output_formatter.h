#ifndef CPPXDIC_PIPELINE_DIC2D_OUTPUT_FORMATTER_H
#define CPPXDIC_PIPELINE_DIC2D_OUTPUT_FORMATTER_H

#include "config.h"
#include "parameters.h"
#include <vector>

namespace cppxdic::pipeline {

class Dic2DOutputFormatter {
public:
    Dic2DOutputFormatter(const Config& config,
                         const BaseParameters& base_params,
                         const StepParameters& tracking_params);

    void format(int stereopair,
                const std::vector<int>& pair_order,
                bool pair_forced) const;

private:
    const Config& config_;
    const BaseParameters& base_params_;
    const StepParameters& tracking_params_;
};

} // namespace cppxdic::pipeline

#endif // CPPXDIC_PIPELINE_DIC2D_OUTPUT_FORMATTER_H
