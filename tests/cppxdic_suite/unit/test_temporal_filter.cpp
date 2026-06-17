/**
 * @file test_temporal_filter.cpp
 * @brief Unit tests for filterTime (include/temporal_filter.h, src/temporal_filter.cpp).
 *
 * A zero-phase 4th-order Butterworth low-pass filter (filtfilt). Intended invariants:
 *  - constant (DC) signal is preserved (DC gain == 1).
 *  - DC + high-frequency component: the high-frequency component is attenuated, DC retained.
 *  - very short series do not crash and are returned with the same shape.
 *
 * FIXED: the production filtfilt/butterLowPass instability has been corrected
 * (butterLowPass emitted the denominator coefficients in reversed order, and filtfilt
 * used zero initial conditions). butterLowPass now produces normalized descending-power
 * coefficients and filtfilt injects MATLAB/scipy-style steady-state initial conditions,
 * so a constant (DC) input is preserved and high frequencies are attenuated. The two
 * invariants below are asserted as live regression guards.
 */

#include "../framework/test_harness.h"
#include "temporal_filter.h"

#include <cmath>
#include <vector>

using cppxdic::filterTime;

TEST(temporal_filter, constant_signal_preserved) {
    // A zero-phase Butterworth low-pass has DC gain == 1, so a constant input
    // must be returned unchanged (this used to diverge to ~1e+24).
    std::vector<std::vector<double>> data(1, std::vector<double>(64, 5.0));
    auto out = filterTime(data, 5.0, 50.0);
    CHECK_EQ(out.size(), static_cast<size_t>(1));
    CHECK_EQ(out[0].size(), static_cast<size_t>(64));
    for (double v : out[0]) CHECK_NEAR(v, 5.0, 1e-3);
}

TEST(temporal_filter, high_freq_attenuated_dc_retained) {
    // DC + 20 Hz sine at fs=50 (cutoff 5 Hz): the 20 Hz component sits well above
    // the cutoff, so it must be strongly attenuated while the DC offset is retained.
    const size_t n = 128;
    const double fs = 50.0, f_hi = 20.0, dc = 3.0, amp = 1.0;
    std::vector<std::vector<double>> data(1, std::vector<double>(n));
    for (size_t i = 0; i < n; ++i) {
        data[0][i] = dc + amp * std::sin(2.0 * M_PI * f_hi * static_cast<double>(i) / fs);
    }

    auto out = filterTime(data, 5.0, 50.0);
    CHECK_EQ(out[0].size(), n);

    // Mean (DC offset) preserved.
    double mean = 0.0;
    for (double v : out[0]) mean += v;
    mean /= static_cast<double>(n);
    CHECK_NEAR(mean, dc, 1e-2);

    // High-frequency ripple strongly attenuated: look at the interior to avoid
    // any residual edge effects, and require the residual amplitude to be a
    // small fraction of the input amplitude.
    double max_dev = 0.0;
    for (size_t i = 16; i + 16 < n; ++i) {
        max_dev = std::max(max_dev, std::fabs(out[0][i] - dc));
    }
    CHECK_TRUE(max_dev < 0.2 * amp);
}

TEST(temporal_filter, very_short_series_no_crash) {
    // Single-sample series must be returned unchanged (no crash). This path is stable.
    std::vector<std::vector<double>> data(1, std::vector<double>{7.0});
    auto out = filterTime(data, 5.0, 50.0);
    CHECK_EQ(out.size(), static_cast<size_t>(1));
    CHECK_EQ(out[0].size(), static_cast<size_t>(1));
}

TEST_MAIN()
