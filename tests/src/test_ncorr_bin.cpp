/**
 * Test program to analyze ncorr .bin files
 * Provides detailed statistics about u, v, and cc fields
 */

#include "dic_structures.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>

using namespace cppxdic;

struct FieldStats {
    double min_val;
    double max_val;
    double mean_val;
    double median_val;
    size_t num_zeros;
    size_t num_nans;
    size_t num_infs;
    size_t total_count;
    
    FieldStats() : min_val(std::numeric_limits<double>::infinity()),
                   max_val(-std::numeric_limits<double>::infinity()),
                   mean_val(0.0), median_val(0.0),
                   num_zeros(0), num_nans(0), num_infs(0), total_count(0) {}
};

FieldStats computeStats(const std::vector<double>& data) {
    FieldStats stats;
    stats.total_count = data.size();
    
    if (data.empty()) return stats;
    
    std::vector<double> valid_data;
    valid_data.reserve(data.size());
    
    double sum = 0.0;
    
    for (const auto& val : data) {
        if (std::isnan(val)) {
            stats.num_nans++;
        } else if (std::isinf(val)) {
            stats.num_infs++;
        } else {
            if (std::abs(val) < 1e-10) {
                stats.num_zeros++;
            }
            valid_data.push_back(val);
            sum += val;
            stats.min_val = std::min(stats.min_val, val);
            stats.max_val = std::max(stats.max_val, val);
        }
    }
    
    if (!valid_data.empty()) {
        stats.mean_val = sum / valid_data.size();
        
        // Compute median
        std::vector<double> sorted_data = valid_data;
        std::sort(sorted_data.begin(), sorted_data.end());
        size_t mid = sorted_data.size() / 2;
        if (sorted_data.size() % 2 == 0) {
            stats.median_val = (sorted_data[mid - 1] + sorted_data[mid]) / 2.0;
        } else {
            stats.median_val = sorted_data[mid];
        }
    } else {
        stats.min_val = std::numeric_limits<double>::quiet_NaN();
        stats.max_val = std::numeric_limits<double>::quiet_NaN();
        stats.mean_val = std::numeric_limits<double>::quiet_NaN();
        stats.median_val = std::numeric_limits<double>::quiet_NaN();
    }
    
    return stats;
}

void printStats(const std::string& name, const FieldStats& stats) {
    std::cout << "  " << name << ":\n";
    std::cout << "    Total elements: " << stats.total_count << "\n";
    std::cout << "    Min:     " << std::setw(12) << std::setprecision(6) << std::fixed << stats.min_val << "\n";
    std::cout << "    Max:     " << std::setw(12) << std::setprecision(6) << std::fixed << stats.max_val << "\n";
    std::cout << "    Mean:    " << std::setw(12) << std::setprecision(6) << std::fixed << stats.mean_val << "\n";
    std::cout << "    Median:  " << std::setw(12) << std::setprecision(6) << std::fixed << stats.median_val << "\n";
    std::cout << "    Zeros:   " << stats.num_zeros << " (" 
              << std::setprecision(2) << (100.0 * stats.num_zeros / stats.total_count) << "%)\n";
    std::cout << "    NaNs:    " << stats.num_nans << " (" 
              << std::setprecision(2) << (100.0 * stats.num_nans / stats.total_count) << "%)\n";
    std::cout << "    Infs:    " << stats.num_infs << " (" 
              << std::setprecision(2) << (100.0 * stats.num_infs / stats.total_count) << "%)\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_bin_file>" << std::endl;
        std::cerr << "Example: " << argv[0] << " DIC3Dcombined_2Pairs_stitched.bin" << std::endl;
        return 1;
    }
    
    std::string bin_path = argv[1];
    
    std::cout << "========================================\n";
    std::cout << "NCORR Binary File Analyzer\n";
    std::cout << "========================================\n";
    std::cout << "File: " << bin_path << "\n\n";
    
    try {
        // Load the binary file
        DIC3Dcombined data = DIC3Dcombined::loadBinary(bin_path);
        
        // Basic info
        std::cout << "Basic Information:\n";
        std::cout << "  Number of frames: " << data.Points3D.size() << "\n";
        if (!data.Points3D.empty()) {
            std::cout << "  Points per frame: " << data.Points3D[0].x.size() << "\n";
        }
        std::cout << "  Number of faces: " << (data.Faces.size() / 3) << "\n";
        std::cout << "  Number of pairs: " << data.pairIndices.size() << "\n\n";
        
        // Displacement vector info
        std::cout << "Displacement Data (DispVec):\n";
        std::cout << "  Number of frame entries: " << data.Disp.DispVec.size() << "\n";
        if (!data.Disp.DispVec.empty()) {
            std::cout << "  Size of first frame: " << data.Disp.DispVec[0].size() 
                      << " (should be " << (data.Points3D.empty() ? 0 : data.Points3D[0].x.size() * 3) << " for 3D vectors)\n";
        }
        
        // Displacement magnitude info
        std::cout << "\nDisplacement Magnitude (DispMgn):\n";
        std::cout << "  Number of frame entries: " << data.Disp.DispMgn.size() << "\n";
        if (!data.Disp.DispMgn.empty()) {
            std::cout << "  Size of first frame: " << data.Disp.DispMgn[0].size() << "\n";
        }
        
        // Correlation coefficient info
        std::cout << "\nFace Correlation (FaceCorrComb):\n";
        std::cout << "  Number of frame entries: " << data.FaceCorrComb.size() << "\n";
        if (!data.FaceCorrComb.empty()) {
            std::cout << "  Size of first frame: " << data.FaceCorrComb[0].size() 
                      << " (should be " << (data.Faces.size() / 3) << " faces)\n";
        }
        
        std::cout << "\n========================================\n";
        std::cout << "Per-Frame Statistics\n";
        std::cout << "========================================\n\n";
        
        // Per-frame analysis
        size_t num_frames = data.Points3D.size();
        size_t frames_to_show = std::min(num_frames, size_t(10));  // Show first 10 and last 10
        
        for (size_t frame = 0; frame < num_frames; ++frame) {
            // Skip middle frames if there are many
            if (num_frames > 20 && frame >= 10 && frame < num_frames - 10) {
                if (frame == 10) {
                    std::cout << "  ... (skipping frames 10 to " << (num_frames - 11) << ") ...\n\n";
                }
                continue;
            }
            
            std::cout << "Frame " << frame << ":\n";
            
            // Analyze DispMgn if available
            if (frame < data.Disp.DispMgn.size() && !data.Disp.DispMgn[frame].empty()) {
                FieldStats disp_stats = computeStats(data.Disp.DispMgn[frame]);
                printStats("DispMgn", disp_stats);
            } else {
                std::cout << "  DispMgn: No data\n";
            }
            
            // Analyze DispVec components if available
            if (frame < data.Disp.DispVec.size() && !data.Disp.DispVec[frame].empty()) {
                const auto& disp_vec = data.Disp.DispVec[frame];
                size_t n_points = disp_vec.size() / 3;
                
                if (disp_vec.size() >= 3) {
                    // Extract U, V, W components
                    std::vector<double> u_data, v_data, w_data;
                    u_data.reserve(n_points);
                    v_data.reserve(n_points);
                    w_data.reserve(n_points);
                    
                    for (size_t i = 0; i < n_points; ++i) {
                        if (i * 3 + 2 < disp_vec.size()) {
                            u_data.push_back(disp_vec[i * 3]);
                            v_data.push_back(disp_vec[i * 3 + 1]);
                            w_data.push_back(disp_vec[i * 3 + 2]);
                        }
                    }
                    
                    FieldStats u_stats = computeStats(u_data);
                    FieldStats v_stats = computeStats(v_data);
                    FieldStats w_stats = computeStats(w_data);
                    
                    printStats("DispVec U", u_stats);
                    printStats("DispVec V", v_stats);
                    printStats("DispVec W", w_stats);
                }
            } else {
                std::cout << "  DispVec: No data\n";
            }
            
            // Analyze correlation coefficients if available
            if (frame < data.FaceCorrComb.size() && !data.FaceCorrComb[frame].empty()) {
                FieldStats corr_stats = computeStats(data.FaceCorrComb[frame]);
                printStats("FaceCorrComb", corr_stats);
            } else {
                std::cout << "  FaceCorrComb: No data\n";
            }
            
            std::cout << "\n";
        }
        
        std::cout << "========================================\n";
        std::cout << "Analysis Complete\n";
        std::cout << "========================================\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
