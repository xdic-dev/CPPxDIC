/**
 * @file cuncorr_adapter_test.cpp
 * @brief Synthetic recovery test for the cuNCorr -> ncorr::DIC_analysis_output adapter.
 *
 * Builds an analytically-shifted frame pair, runs run_cuncorr_dic, and checks that the
 * resulting Disp2D (on the reduced grid, unpacked exactly like singledic/StepE do) recovers
 * the known translation. This exercises the grid/ROI packaging that the downstream pipeline
 * depends on. CPU backend on this machine; on a GPU node it validates the CUDA path too
 * (cuNCorr's own cuda_parity_test guarantees CUDA==CPU).
 *
 * Build: cmake -DBUILD_CUNCORR_ADAPTER_TEST=ON ..  &&  cmake --build . --target cuncorr_adapter_test
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "dic/cuncorr_dic.h"

namespace {
constexpr double PI = 3.14159265358979323846;
double tex(double r, double c) {
    return 0.5 + 0.18 * std::sin(2 * PI * r / 12.0) + 0.18 * std::cos(2 * PI * c / 10.0) +
           0.10 * std::sin(2 * PI * (r + c) / 15.0);
}
double median(std::vector<double> v) {
    if (v.empty()) return std::nan("");
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
}  // namespace

int main() {
    const int H = 200, W = 200;
    const double u0 = 0.6, v0 = -0.4;  // u = col/x disp, v = row/y disp

    // Analytic shift: def(r,c) = tex(r - v0, c - u0) — exact, no interpolation error.
    ncorr::Array2D<double> ref(H, W), def(H, W);
    for (int r = 0; r < H; ++r)
        for (int c = 0; c < W; ++c) {
            ref(r, c) = tex(r, c);
            def(r, c) = tex(r - v0, c - u0);
        }

    std::vector<ncorr::Image2D> imgs;
    imgs.emplace_back(ref);
    imgs.emplace_back(def);
    ncorr::ROI2D roi(ncorr::Array2D<bool>(H, W, true));

    cppxdic::CuncorrDicConfig cfg;
    cfg.scalefactor = 4;
    cfg.subregion_radius = 16;
    cfg.seed_search = 5;
    cppxdic::CuncorrDicInfo info;

    const ncorr::DIC_analysis_output out = cppxdic::run_cuncorr_dic(imgs, roi, cfg, &info);

    if (out.disps.size() != 1) {
        std::printf("FAIL: expected 1 disp, got %zu\n", out.disps.size());
        return 1;
    }
    // Unpack the reduced-grid Disp2D exactly like singledic / StepE.
    const ncorr::Disp2D& d = out.disps[0];
    const auto& ua = d.get_u().get_array();
    const auto& va = d.get_v().get_array();
    const auto& mask = d.get_roi().get_mask();
    std::vector<double> us, vs;
    for (int i = 0; i < ua.height(); ++i)
        for (int j = 0; j < ua.width(); ++j)
            if (mask(i, j)) { us.push_back(ua(i, j)); vs.push_back(va(i, j)); }

    const double mu = median(us), mv = median(vs);
    std::printf("adapter: backend=%s perspective=%s units=%s grid=%lldx%lld points=%zu\n",
                info.backend.c_str(),
                out.perspective_type == ncorr::PERSPECTIVE::LAGRANGIAN ? "LAGRANGIAN" : "EULERIAN",
                out.units.c_str(), (long long)ua.width(), (long long)ua.height(), us.size());
    std::printf("  median_u=%.4f (want %.2f)   median_v=%.4f (want %.2f)\n", mu, u0, mv, v0);

    const bool ok = us.size() > 1000 && std::fabs(mu - u0) < 0.05 && std::fabs(mv - v0) < 0.05 &&
                    out.perspective_type == ncorr::PERSPECTIVE::LAGRANGIAN && out.units == "pixels";
    std::printf(ok ? "ADAPTER OK\n" : "ADAPTER FAIL\n");
    return ok ? 0 : 1;
}
