/**
 * @file cuncorr_dic.cpp
 * @brief cuNCorr -> ncorr::DIC_analysis_output adapter (see cuncorr_dic.h).
 */

#include "dic/cuncorr_dic.h"

#include <cmath>
#include <cstdint>
#include <stdexcept>

#include "cuncorr/backend.h"
#include "cuncorr/session.h"

namespace cppxdic {
namespace {

using diff_t = ncorr::ROI2D::difference_type;

/// ncorr grayscale Image2D ([0,1]) -> row-major 8-bit buffer for a cuncorr::ImageBuffer.
std::vector<std::uint8_t> image_to_u8(const ncorr::Image2D& img, int& W, int& H) {
    const ncorr::Array2D<double> gs = img.get_gs();  // (height, width), values in [0,1]
    H = static_cast<int>(gs.height());
    W = static_cast<int>(gs.width());
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(W) * H);
    for (int r = 0; r < H; ++r)
        for (int c = 0; c < W; ++c) {
            double v = gs(r, c) * 255.0;
            v = v < 0 ? 0 : (v > 255 ? 255 : v);
            buf[static_cast<std::size_t>(r) * W + c] = static_cast<std::uint8_t>(v + 0.5);
        }
    return buf;
}

/// ncorr full-resolution ROI mask -> row-major 8-bit buffer (255 = analysed).
std::vector<std::uint8_t> roi_to_u8(const ncorr::ROI2D& roi) {
    const ncorr::Array2D<bool>& m = roi.get_mask();
    const int H = static_cast<int>(m.height()), W = static_cast<int>(m.width());
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(W) * H);
    for (int r = 0; r < H; ++r)
        for (int c = 0; c < W; ++c)
            buf[static_cast<std::size_t>(r) * W + c] = m(r, c) ? 255 : 0;
    return buf;
}

}  // namespace

bool cuncorr_cuda_available() {
    auto be = cuncorr::make_default_backend();
    const auto info = be->info();
    return info.kind == cuncorr::Backend::CUDA && info.device_available;
}

ncorr::DIC_analysis_output run_cuncorr_dic(const std::vector<ncorr::Image2D>& imgs,
                                           const ncorr::ROI2D& roi, const CuncorrDicConfig& cfg,
                                           CuncorrDicInfo* info) {
    if (imgs.size() < 2)
        throw std::invalid_argument("run_cuncorr_dic: need at least a reference + one frame");
    const int sf = cfg.scalefactor > 0 ? cfg.scalefactor : 1;

    cuncorr::SessionConfig scfg;
    scfg.scalefactor = sf;
    scfg.subregion_radius = cfg.subregion_radius;
    scfg.num_threads = cfg.num_threads;
    scfg.seed_x = cfg.seed_x;
    scfg.seed_y = cfg.seed_y;
    scfg.seed_search = cfg.seed_search;
    scfg.debug = cfg.debug;

    if (info) {
        auto be = cuncorr::make_default_backend();
        const auto bi = be->info();
        info->backend = bi.name;
        info->cuda = bi.kind == cuncorr::Backend::CUDA && bi.device_available;
    }

    cuncorr::NcorrSession session(scfg);

    int W = 0, H = 0;
    const auto ref_buf = image_to_u8(imgs[0], W, H);
    session.set_reference(cuncorr::ImageBuffer(ref_buf.data(), W, H, 1));

    const auto roi_buf = roi_to_u8(roi);
    session.set_roi(cuncorr::ImageBuffer(roi_buf.data(), static_cast<int>(roi.width()),
                                         static_cast<int>(roi.height()), 1));

    // Reduced ROI/grid — cuNCorr's reduced dims equal these by construction.
    const ncorr::ROI2D roi_reduced = roi.reduce(sf);
    const diff_t gH = roi_reduced.height();
    const diff_t gW = roi_reduced.width();

    session.reset_sequence();
    std::vector<ncorr::Disp2D> disps;
    disps.reserve(imgs.size() - 1);

    for (std::size_t f = 1; f < imgs.size(); ++f) {
        int dW = 0, dH = 0;
        const auto def_buf = image_to_u8(imgs[f], dW, dH);
        const cuncorr::DICResult res =
            session.process_frame_sequence(cuncorr::ImageBuffer(def_buf.data(), dW, dH, 1));

        ncorr::Array2D<double> A_v(gH, gW), A_u(gH, gW), A_cc(gH, gW);
        ncorr::Array2D<bool> A_vp(gH, gW, false);

        const int rgW = res.width, rgH = res.height;
        for (int gi = 0; gi < static_cast<int>(gH); ++gi)
            for (int gj = 0; gj < static_cast<int>(gW); ++gj) {
                if (gi >= rgH || gj >= rgW) continue;
                const std::size_t idx = static_cast<std::size_t>(gi) * rgW + gj;
                const double v = res.v[idx], u = res.u[idx], cc = res.corrcoef[idx];
                if (std::isnan(v) || std::isnan(u)) continue;
                A_v(gi, gj) = v;   // v = row/y displacement (matches ncorr)
                A_u(gi, gj) = u;   // u = col/x displacement
                A_cc(gi, gj) = std::isnan(cc) ? 0.0 : cc;
                A_vp(gi, gj) = true;
            }

        const ncorr::ROI2D roi_valid = roi_reduced.form_union(A_vp);
        disps.emplace_back(std::move(A_v), std::move(A_u), std::move(A_cc), roi_valid,
                           static_cast<diff_t>(sf));
    }

    return ncorr::DIC_analysis_output(disps, ncorr::PERSPECTIVE::LAGRANGIAN, "pixels", 1.0);
}

}  // namespace cppxdic
