#pragma once
/**
 * @file cuncorr_dic.h
 * @brief Adapter: run the cuNCorr DIC engine and package results as ncorr::DIC_analysis_output.
 *
 * This is the integration seam for cppxdic -> cuNCorr. cuNCorr matches CppNCorr's reduced-grid
 * convention exactly (reduced node (r,c) <-> full pixel (r*scalefactor, c*scalefactor), and
 * (dim-1)/sf+1 == ceil(dim/sf)), so its per-frame displacement fields drop straight into
 * ncorr::Disp2D. The returned DIC_analysis_output is raw Lagrangian, pixel-unit, on the reduced
 * grid — structurally identical to ncorr::matlab_DIC_analysis_*, so the .bin/StepE pipeline
 * consumes it unchanged.
 *
 * cuNCorr internally runs on CUDA when a device is present, else its CPU backend; both share the
 * same math cores and are numerically identical.
 */

#include <string>
#include <vector>

#include "ncorr.h" // DIC_analysis_output, Image2D, ROI2D, Disp2D, PERSPECTIVE

namespace cppxdic {

/// Tunables mirrored onto cuncorr::SessionConfig. scalefactor MUST equal the ncorr
/// scalefactor used elsewhere (singledic uses spacing+1) so the grids align.
struct CuncorrDicConfig {
    int scalefactor = 4;
    int subregion_radius = 20;
    int num_threads = 4;
    int seed_x = -1;      ///< manual seed column (full-image px); -1 = auto
    int seed_y = -1;      ///< manual seed row (full-image px); -1 = auto
    int seed_search = 15; ///< coarse ZNCC seed search radius (px)
    bool debug = false;
};

/// Which backend actually ran.
struct CuncorrDicInfo {
    std::string backend; ///< e.g. "cpu" or "cuda:0 <name>"
    bool cuda = false;   ///< true if a CUDA device executed the run
};

/// True if cuNCorr was built with CUDA and a device is present at runtime.
bool cuncorr_cuda_available();

/// Run cuNCorr in sequence mode (fixed reference = imgs[0], warm-start each frame) over the
/// stack against the full-resolution @p roi. Returns a raw Lagrangian, pixel-unit
/// DIC_analysis_output on the reduced grid (one Disp2D per current frame).
/// @throws std::invalid_argument if fewer than 2 images.
ncorr::DIC_analysis_output run_cuncorr_dic(const std::vector<ncorr::Image2D>& imgs,
                                           const ncorr::ROI2D& roi, const CuncorrDicConfig& cfg,
                                           CuncorrDicInfo* info = nullptr);

} // namespace cppxdic
