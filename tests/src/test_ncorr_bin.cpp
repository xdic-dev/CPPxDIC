/**
 * Test program to analyze ncorr .bin files
 * Supports both ncorr DIC_analysis_output and DIC3Dcombined formats
 * Provides detailed statistics about u, v, and cc fields
 */

#include "dic_structures.h"
#include <ncorr.h>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <limits>
#include <fstream>

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

void analyzeNcorrDICOutput(const std::string& bin_path) {
    std::cout << "Attempting to load as ncorr::DIC_analysis_output format...\n\n";
    
    ncorr::DIC_analysis_output dic_output = ncorr::DIC_analysis_output::load(bin_path);
    
    // Basic info
    std::cout << "Basic Information:\n";
    std::cout << "  Number of frames: " << dic_output.disps.size() << "\n";
    std::cout << "  Perspective type: " << (dic_output.perspective_type == ncorr::PERSPECTIVE::LAGRANGIAN ? "Lagrangian" : "Eulerian") << "\n";
    std::cout << "  Units: " << dic_output.units << "\n";
    std::cout << "  Units per pixel: " << dic_output.units_per_pixel << "\n";
    
    if (!dic_output.disps.empty()) {
        const auto& first_disp = dic_output.disps[0];
        const auto& u_array = first_disp.get_u().get_array();
        const auto& v_array = first_disp.get_v().get_array();
        const auto& cc_array = first_disp.get_cc().get_array();
        
        std::cout << "  Field dimensions (HxW): " << u_array.height() << " x " << u_array.width() << "\n";
        std::cout << "  Total elements per frame: " << (u_array.height() * u_array.width()) << "\n\n";
    }
    
    std::cout << "========================================\n";
    std::cout << "Per-Frame Statistics\n";
    std::cout << "========================================\n\n";
    
    // Per-frame analysis
    size_t num_frames = dic_output.disps.size();
    size_t frames_to_show = std::min(num_frames, size_t(10));
    
    for (size_t frame = 0; frame < num_frames; ++frame) {
        // Skip middle frames if there are many
        if (num_frames > 20 && frame >= 10 && frame < num_frames - 10) {
            if (frame == 10) {
                std::cout << "  ... (skipping frames 10 to " << (num_frames - 11) << ") ...\n\n";
            }
            continue;
        }
        
        std::cout << "Frame " << frame << ":\n";
        
        const auto& disp = dic_output.disps[frame];
        const auto& u_array = disp.get_u().get_array();
        const auto& v_array = disp.get_v().get_array();
        const auto& cc_array = disp.get_cc().get_array();
        const auto& roi_mask = disp.get_roi().get_mask();
        
        size_t height = u_array.height();
        size_t width = u_array.width();
        
        // Extract data from arrays
        std::vector<double> u_data, v_data, cc_data;
        u_data.reserve(height * width);
        v_data.reserve(height * width);
        cc_data.reserve(height * width);
        
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                if (roi_mask(y, x)) {
                    u_data.push_back(u_array(y, x));
                    v_data.push_back(v_array(y, x));
                    cc_data.push_back(cc_array(y, x));
                }
            }
        }
        
        FieldStats u_stats = computeStats(u_data);
        FieldStats v_stats = computeStats(v_data);
        FieldStats cc_stats = computeStats(cc_data);
        
        printStats("U displacement", u_stats);
        printStats("V displacement", v_stats);
        printStats("Correlation Coeff", cc_stats);
        
        std::cout << "\n";
    }
    
    std::cout << "========================================\n";
    std::cout << "Analysis Complete\n";
    std::cout << "========================================\n";
}

void analyzeDIC3Dcombined(const std::string& bin_path) {
    std::cout << "Attempting to load as DIC3Dcombined format...\n\n";
    
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
    size_t frames_to_show = std::min(num_frames, size_t(10));
    
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
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_bin_file>" << std::endl;
        std::cerr << "\nSupported formats:" << std::endl;
        std::cerr << "  - ncorr*.bin (ncorr::DIC_analysis_output)" << std::endl;
        std::cerr << "  - DIC3Dcombined*.bin (DIC3Dcombined results)" << std::endl;
        std::cerr << "\nExample: " << argv[0] << " ncorr1.bin" << std::endl;
        return 1;
    }
    
    std::string bin_path = argv[1];
    
    std::cout << "========================================\n";
    std::cout << "NCORR Binary File Analyzer\n";
    std::cout << "========================================\n";
    std::cout << "File: " << bin_path << "\n\n";
    
    try {
        // Try to detect format by checking magic number
        std::ifstream ifs(bin_path, std::ios::binary);
        if (!ifs.is_open()) {
            throw std::runtime_error("Cannot open file: " + bin_path);
        }
        
        uint32_t magic = 0;
        ifs.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
        ifs.close();
        
        // DIC3Dcombined files have magic number 0x44494333 ("DIC3")
        if (magic == 0x44494333) {
            analyzeDIC3Dcombined(bin_path);
        } else {
            // Try ncorr format
            try {
                analyzeNcorrDICOutput(bin_path);
            } catch (const std::exception& e) {
                std::cerr << "Failed to load as ncorr format: " << e.what() << "\n\n";
                std::cerr << "File may be corrupted or in an unsupported format.\n";
                std::cerr << "\nSupported formats:\n";
                std::cerr << "  - ncorr::DIC_analysis_output (.bin from ncorr library)\n";
                std::cerr << "  - DIC3Dcombined (with magic number 0x44494333)\n";
                return 1;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
