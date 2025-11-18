/**
 * Temporal Filtering for CPPXDIC
 * Port of MATLAB myfilterTime.m
 * 
 * Implements Butterworth low-pass filtering for smoothing displacement time series
 */

#ifndef TEMPORAL_FILTER_H
#define TEMPORAL_FILTER_H

#include <vector>
#include <cmath>
#include <limits>

namespace cppxdic {

/**
 * Apply temporal Butterworth low-pass filter to displacement data
 * 
 * @param data Input data (nPoints x nFrames) - each column is a time series
 * @param freq_filt Filter cutoff frequency (Hz)
 * @param freq_acq Acquisition/sampling frequency (Hz)
 * @param nPoints Number of spatial points
 * @param nFrames Number of time frames
 * @return Filtered data (same dimensions as input)
 * 
 * Algorithm:
 * - 4th order Butterworth low-pass filter
 * - Zero-phase filtering using forward-backward pass (filtfilt)
 * - Boundary padding to reduce edge effects
 * - Preserves NaN values (only filters valid data)
 */
std::vector<std::vector<double>> filterTime(
    const std::vector<std::vector<double>>& data,
    double freq_filt = 10.0,
    double freq_acq = 50.0
);

/**
 * Apply temporal filtering to 3D displacement vectors
 * 
 * @param disp_x X-component displacement (nPoints x nFrames)
 * @param disp_y Y-component displacement (nPoints x nFrames)
 * @param disp_z Z-component displacement (nPoints x nFrames)
 * @param freq_filt Filter cutoff frequency (Hz)
 * @param freq_acq Acquisition/sampling frequency (Hz)
 * @return Tuple of (filtered_x, filtered_y, filtered_z)
 */
std::tuple<
    std::vector<std::vector<double>>,
    std::vector<std::vector<double>>,
    std::vector<std::vector<double>>
> filterTime3D(
    const std::vector<std::vector<double>>& disp_x,
    const std::vector<std::vector<double>>& disp_y,
    const std::vector<std::vector<double>>& disp_z,
    double freq_filt = 10.0,
    double freq_acq = 50.0
);

namespace detail {

/**
 * Design Butterworth filter coefficients
 * @param order Filter order (typically 4)
 * @param wn Normalized cutoff frequency (0 to 1, where 1 is Nyquist)
 * @return Pair of (numerator coefficients B, denominator coefficients A)
 */
std::pair<std::vector<double>, std::vector<double>> 
butterLowPass(int order, double wn);

/**
 * Zero-phase digital filtering (MATLAB filtfilt equivalent)
 * @param b Numerator coefficients
 * @param a Denominator coefficients
 * @param x Input signal
 * @return Filtered signal (same length as input)
 */
std::vector<double> filtfilt(
    const std::vector<double>& b,
    const std::vector<double>& a,
    const std::vector<double>& x
);

/**
 * Forward IIR filter
 */
std::vector<double> filter(
    const std::vector<double>& b,
    const std::vector<double>& a,
    const std::vector<double>& x
);

} // namespace detail

} // namespace cppxdic

#endif // TEMPORAL_FILTER_H
