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

namespace {

std::vector<double> padAndFilterSeries(
    const std::vector<double>& values,
    const std::vector<double>& b,
    const std::vector<double>& a
) {
    const size_t n_frames = values.size();
    const int npad = 5;

    std::vector<double> padded;
    padded.reserve(n_frames + 2 * npad);
    for (int index = 0; index < npad; ++index) {
        padded.push_back(values.front());
    }
    padded.insert(padded.end(), values.begin(), values.end());
    for (int index = 0; index < npad; ++index) {
        padded.push_back(values.back());
    }

    std::vector<double> filtered = filtfilt(b, a, padded);
    std::vector<double> result(n_frames, std::numeric_limits<double>::quiet_NaN());
    for (size_t frame = 0; frame < n_frames; ++frame) {
        result[frame] = filtered[npad + frame];
    }
    return result;
}

void fillMissingLinear(std::vector<double>& data) {
    const size_t n = data.size();
    if (n == 0) {
        return;
    }

    size_t first_valid = 0;
    while (first_valid < n && std::isnan(data[first_valid])) {
        ++first_valid;
    }
    if (first_valid == n) {
        return;
    }

    for (size_t index = 0; index < first_valid; ++index) {
        data[index] = data[first_valid];
    }

    size_t last_valid = n - 1;
    while (last_valid > 0 && std::isnan(data[last_valid])) {
        --last_valid;
    }

    for (size_t index = last_valid + 1; index < n; ++index) {
        data[index] = data[last_valid];
    }

    size_t index = first_valid;
    while (index < last_valid) {
        if (!std::isnan(data[index])) {
            ++index;
            continue;
        }

        size_t next_valid = index + 1;
        while (next_valid < n && std::isnan(data[next_valid])) {
            ++next_valid;
        }

        const double v0 = data[index - 1];
        const double v1 = data[next_valid];
        const double span = static_cast<double>(next_valid - index + 1);
        for (size_t interp = index; interp < next_valid; ++interp) {
            const double t = static_cast<double>(interp - index + 1) / span;
            data[interp] = v0 + t * (v1 - v0);
        }
        index = next_valid;
    }
}

} // namespace

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
        
        filtered[ipt] = padAndFilterSeries(data[ipt], b, a);
    }
    
    return filtered;
}

std::vector<std::vector<double>> filterTimeFrameMajor(
    const std::vector<std::vector<double>>& data,
    double freq_filt,
    double freq_acq,
    bool interpolate_missing
) {
    if (data.empty() || data.front().empty()) {
        return data;
    }

    const size_t n_frames = data.size();
    const size_t n_points = data.front().size();

    std::vector<std::vector<double>> point_major(n_points, std::vector<double>(n_frames, std::numeric_limits<double>::quiet_NaN()));
    for (size_t frame = 0; frame < n_frames; ++frame) {
        for (size_t point = 0; point < std::min(n_points, data[frame].size()); ++point) {
            point_major[point][frame] = data[frame][point];
        }
    }

    if (interpolate_missing) {
        double wn = freq_filt / (freq_acq / 2.0);
        if (wn >= 1.0) wn = 0.99;
        if (wn <= 0.0) wn = 0.01;

        auto [b, a] = butterLowPass(4, wn);
        for (auto& series : point_major) {
            bool has_valid = false;
            for (const double value : series) {
                if (!std::isnan(value)) {
                    has_valid = true;
                    break;
                }
            }
            if (!has_valid) {
                continue;
            }

            fillMissingLinear(series);
            series = padAndFilterSeries(series, b, a);
        }
    } else {
        point_major = filterTime(point_major, freq_filt, freq_acq);
    }

    std::vector<std::vector<double>> frame_major(n_frames, std::vector<double>(n_points, std::numeric_limits<double>::quiet_NaN()));
    for (size_t point = 0; point < n_points; ++point) {
        for (size_t frame = 0; frame < n_frames; ++frame) {
            frame_major[frame][point] = point_major[point][frame];
        }
    }

    return frame_major;
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
