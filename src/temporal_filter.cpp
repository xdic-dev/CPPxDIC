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
            // Keep NaN values as is
            filtered[ipt] = data[ipt];
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
        
        // Denominator (from poles)
        // Simplified computation for 4th order
        a.resize(5);
        a[0] = 1.0;
        
        // Multiply out (z - pole_i) terms
        std::vector<std::complex<double>> poly(5, 0.0);
        poly[0] = 1.0;
        
        for (const auto& zp : z_poles) {
            for (int i = 4; i >= 1; --i) {
                poly[i] = poly[i-1] - zp * poly[i];
            }
            poly[0] = -zp * poly[0];
        }
        
        for (int i = 0; i < 5; ++i) {
            a[i] = poly[i].real();
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

std::vector<double> filter(
    const std::vector<double>& b,
    const std::vector<double>& a,
    const std::vector<double>& x
) {
    size_t n = x.size();
    size_t nb = b.size();
    size_t na = a.size();
    
    std::vector<double> y(n, 0.0);
    
    // Normalize by a[0]
    double a0 = a[0];
    
    for (size_t i = 0; i < n; ++i) {
        double sum = 0.0;
        
        // FIR part (numerator)
        for (size_t j = 0; j < nb; ++j) {
            if (i >= j) {
                sum += b[j] * x[i - j];
            }
        }
        
        // IIR part (denominator)
        for (size_t j = 1; j < na; ++j) {
            if (i >= j) {
                sum -= a[j] * y[i - j];
            }
        }
        
        y[i] = sum / a0;
    }
    
    return y;
}

std::vector<double> filtfilt(
    const std::vector<double>& b,
    const std::vector<double>& a,
    const std::vector<double>& x
) {
    if (x.empty()) return x;
    
    // Forward filter
    std::vector<double> y_forward = filter(b, a, x);
    
    // Reverse the signal
    std::vector<double> y_reversed(y_forward.rbegin(), y_forward.rend());
    
    // Backward filter
    std::vector<double> y_backward = filter(b, a, y_reversed);
    
    // Reverse back
    std::vector<double> y(y_backward.rbegin(), y_backward.rend());
    
    return y;
}

} // namespace detail

} // namespace cppxdic
