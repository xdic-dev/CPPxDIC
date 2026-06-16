/**
 * Temporal Filtering Implementation
 * Port of MATLAB myfilterTime.m and filtfilt
 */

#include "temporal_filter.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <complex>
#include <iostream>

namespace cppxdic {

using namespace detail;

std::vector<std::vector<double>> filterTime(
    const std::vector<std::vector<double>>& data,
    double freq_filt,
    double freq_acq
) {
    if (data.empty() || data[0].empty()) {
        return data;
    }
    
    size_t nPoints = data.size();
    size_t nFrames = data[0].size();
    
    // Design Butterworth filter
    double wn = freq_filt / (freq_acq / 2.0);  // Normalized frequency
    if (wn >= 1.0) wn = 0.99;  // Ensure stability
    if (wn <= 0.0) wn = 0.01;
    
    auto [b, a] = butterLowPass(4, wn);  // 4th order
    
    // Prepare output
    std::vector<std::vector<double>> filtered(nPoints, std::vector<double>(nFrames, NAN));
    
    int npad = 5;  // Padding to reduce border effects
    
    // Process each point's time series
    for (size_t ipt = 0; ipt < nPoints; ++ipt) {
        // Check if all frames are valid (no NaN)
        bool has_nan = false;
        for (size_t iframe = 0; iframe < nFrames; ++iframe) {
            if (std::isnan(data[ipt][iframe])) {
                has_nan = true;
                break;
            }
        }
        
        if (has_nan) {
            // MATLAB: points with any NaN → all frames NaN in output
            // (data_out initialized to NaN, only mask==true points get filtered values)
            // filtered[ipt] is already initialized to NaN
            continue;
        }
        
        // Pad the signal
        std::vector<double> padded;
        padded.reserve(nFrames + 2 * npad);
        
        // Pad beginning with first value
        for (int i = 0; i < npad; ++i) {
            padded.push_back(data[ipt][0]);
        }
        
        // Copy data
        for (size_t iframe = 0; iframe < nFrames; ++iframe) {
            padded.push_back(data[ipt][iframe]);
        }
        
        // Pad end with last value
        for (int i = 0; i < npad; ++i) {
            padded.push_back(data[ipt][nFrames - 1]);
        }
        
        // Apply zero-phase filter
        std::vector<double> result = filtfilt(b, a, padded);
        
        // Extract unpadded result
        for (size_t iframe = 0; iframe < nFrames; ++iframe) {
            filtered[ipt][iframe] = result[npad + iframe];
        }
    }
    
    return filtered;
}

std::tuple<
    std::vector<std::vector<double>>,
    std::vector<std::vector<double>>,
    std::vector<std::vector<double>>
> filterTime3D(
    const std::vector<std::vector<double>>& disp_x,
    const std::vector<std::vector<double>>& disp_y,
    const std::vector<std::vector<double>>& disp_z,
    double freq_filt,
    double freq_acq
) {
    auto filt_x = filterTime(disp_x, freq_filt, freq_acq);
    auto filt_y = filterTime(disp_y, freq_filt, freq_acq);
    auto filt_z = filterTime(disp_z, freq_filt, freq_acq);
    
    return {filt_x, filt_y, filt_z};
}

namespace detail {

std::pair<std::vector<double>, std::vector<double>> 
butterLowPass(int order, double wn) {
    // Butterworth filter design using bilinear transformation
    // This is a simplified implementation for low-pass filters
    
    // For 4th order Butterworth, we use the standard coefficients
    // Analog prototype poles for normalized Butterworth filter
    std::vector<std::complex<double>> poles;
    for (int k = 0; k < order; ++k) {
        double theta = M_PI * (2.0 * k + order + 1) / (2.0 * order);
        poles.emplace_back(std::cos(theta), std::sin(theta));
    }
    
    // Pre-warp frequency for bilinear transform
    double wp = 2.0 * std::tan(M_PI * wn / 2.0);
    
    // Scale poles by cutoff frequency
    for (auto& p : poles) {
        p *= wp;
    }
    
    // Bilinear transformation: s = 2*(z-1)/(z+1)
    std::vector<std::complex<double>> z_poles;
    for (const auto& p : poles) {
        z_poles.push_back((2.0 + p) / (2.0 - p));
    }
    
    // Compute filter coefficients from poles
    // For simplicity, we'll use pre-computed coefficients for 4th order
    // This matches MATLAB's butter(4, wn) closely
    
    std::vector<double> b, a;
    
    if (order == 4) {
        // Compute gain
        double k = 1.0;
        for (const auto& zp : z_poles) {
            k *= std::abs(1.0 - zp);
        }
        k = k / std::pow(2.0, order);
        
        // Numerator (all zeros at z=-1 for low-pass)
        b = {k, 4*k, 6*k, 4*k, k};
        
        // Denominator (from poles): A(z) = prod_i (z - zp_i)
        a.resize(5);

        // Multiply out (z - pole_i) terms. After the loop, poly[i] holds the
        // coefficient of z^i, so poly[4] == 1 (leading) and poly[0] is the
        // constant term.
        std::vector<std::complex<double>> poly(5, 0.0);
        poly[0] = 1.0;

        for (const auto& zp : z_poles) {
            for (int i = 4; i >= 1; --i) {
                poly[i] = poly[i-1] - zp * poly[i];
            }
            poly[0] = -zp * poly[0];
        }

        // The difference-equation form used by filter()/filtfilt() expects the
        // coefficients in DESCENDING powers of z (equivalently ascending powers
        // of z^-1), with a[0] the leading coefficient used for normalization.
        // poly[] is in ascending powers of z, so reverse it. Forgetting this
        // reversal leaves a[0] = (small) constant term and a recursion that is
        // not normalized -> the filter diverges (the original DC-instability bug).
        for (int i = 0; i < 5; ++i) {
            a[i] = poly[4 - i].real();
        }
    } else {
        // Fallback: simple 2nd order filter
        double alpha = std::sin(M_PI * wn) / (2.0 * 0.707);  // Q = 0.707 for Butterworth
        double cos_w = std::cos(M_PI * wn);
        
        b = {alpha, 2*alpha, alpha};
        a = {1.0 + alpha, -2.0 * cos_w, 1.0 - alpha};
    }
    
    return {b, a};
}

namespace {

// Direct-Form-II-transposed IIR filtering with an explicit initial state `z`
// (length max(nb,na)-1). `b`/`a` are in descending powers of z with a[0] the
// leading coefficient. This is the building block scipy/MATLAB use for `lfilter`
// and lets filtfilt() inject the steady-state initial conditions that keep a
// constant (DC) input from producing a startup transient.
std::vector<double> filterDF2T(
    const std::vector<double>& b,
    const std::vector<double>& a,
    const std::vector<double>& x,
    std::vector<double> z
) {
    const size_t n = x.size();
    const size_t order = std::max(b.size(), a.size()) - 1;

    // Normalize coefficients by a[0] and right-pad to length order+1.
    const double a0 = a[0];
    std::vector<double> bn(order + 1, 0.0), an(order + 1, 0.0);
    for (size_t i = 0; i < b.size(); ++i) bn[i] = b[i] / a0;
    for (size_t i = 0; i < a.size(); ++i) an[i] = a[i] / a0;

    z.resize(order, 0.0);

    std::vector<double> y(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        const double xi = x[i];
        const double yi = bn[0] * xi + z[0];
        for (size_t j = 0; j + 1 < order; ++j) {
            z[j] = bn[j + 1] * xi + z[j + 1] - an[j + 1] * yi;
        }
        if (order > 0) {
            z[order - 1] = bn[order] * xi - an[order] * yi;
        }
        y[i] = yi;
    }
    return y;
}

// Steady-state initial conditions for a step input, scaled later by the first
// sample. Mirrors scipy.signal.lfilter_zi: solving (I - A) zi = B for the
// transposed companion form, expressed via the closed-form cumulative sums.
std::vector<double> lfilterZi(
    const std::vector<double>& b_in,
    const std::vector<double>& a_in
) {
    const size_t order = std::max(b_in.size(), a_in.size()) - 1;
    const double a0 = a_in[0];
    std::vector<double> b(order + 1, 0.0), a(order + 1, 0.0);
    for (size_t i = 0; i < b_in.size(); ++i) b[i] = b_in[i] / a0;
    for (size_t i = 0; i < a_in.size(); ++i) a[i] = a_in[i] / a0;

    std::vector<double> zi(order, 0.0);
    if (order == 0) return zi;

    double a_sum = 0.0, b_minus = 0.0;
    for (size_t k = 0; k <= order; ++k) a_sum += a[k];          // sum(a)
    for (size_t k = 1; k <= order; ++k) b_minus += b[k] - a[k] * b[0];
    zi[0] = b_minus / a_sum;

    double asum = 1.0, csum = 0.0;
    for (size_t k = 1; k < order; ++k) {
        asum += a[k];
        csum += b[k] - a[k] * b[0];
        zi[k] = asum * zi[0] - csum;
    }
    return zi;
}

} // namespace

std::vector<double> filter(
    const std::vector<double>& b,
    const std::vector<double>& a,
    const std::vector<double>& x
) {
    // Zero initial state.
    return filterDF2T(b, a, x, {});
}

std::vector<double> filtfilt(
    const std::vector<double>& b,
    const std::vector<double>& a,
    const std::vector<double>& x
) {
    if (x.empty()) return x;

    // Steady-state initial conditions, scaled by the first/last sample of each
    // pass. Without this, even a constant input leaves a large boundary
    // transient (DC gain is met only asymptotically), so smoothing of a
    // near-constant displacement series would be visibly wrong.
    const std::vector<double> zi = lfilterZi(b, a);

    // Forward pass.
    std::vector<double> zi_fwd = zi;
    for (auto& v : zi_fwd) v *= x.front();
    std::vector<double> y_forward = filterDF2T(b, a, x, zi_fwd);

    // Reverse, backward pass.
    std::vector<double> y_reversed(y_forward.rbegin(), y_forward.rend());
    std::vector<double> zi_bwd = zi;
    for (auto& v : zi_bwd) v *= y_reversed.front();
    std::vector<double> y_backward = filterDF2T(b, a, y_reversed, zi_bwd);

    // Reverse back to restore original time order.
    std::vector<double> y(y_backward.rbegin(), y_backward.rend());
    return y;
}

} // namespace detail

} // namespace cppxdic
