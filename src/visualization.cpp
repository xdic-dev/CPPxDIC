/**
 * Implementation of visualization module for DIC results
 */

#include "visualization.h"
#include "dic_structures.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <filesystem>

namespace cppxdic {

Visualization::Visualization(const Config& config) 
    : config_(config) {
    export_format_ = parseExportFormat(config_.export_format);
}

ExportFormat Visualization::parseExportFormat(const std::string& format_str) {
    if (format_str == "vtk" || format_str == "VTK") {
        return ExportFormat::VTK;
    } else if (format_str == "ply" || format_str == "PLY") {
        return ExportFormat::PLY;
    } else if (format_str == "csv" || format_str == "CSV") {
        return ExportFormat::CSV;
    }
    std::cerr << "Warning: Unknown export format '" << format_str 
              << "', defaulting to VTK" << std::endl;
    return ExportFormat::VTK;
}

DIC3DPPresults Visualization::loadFromBinaryCache(const std::string& filepath) {
    std::cout << "Loading DIC results from binary cache: " << filepath << std::endl;
    
    if (!std::filesystem::exists(filepath)) {
        throw std::runtime_error("Binary cache file not found: " + filepath);
    }
    
    // Use the static method from DIC3Dcombined
    DIC3Dcombined combined = DIC3Dcombined::loadBinary(filepath);
    
    // Convert to DIC3DPPresults
    DIC3DPPresults results;
    // Copy base class data
    static_cast<DIC3Dcombined&>(results) = combined;
    
    std::cout << "Loaded " << results.Points3D.size() << " frames" << std::endl;
    
    return results;
}

void Visualization::applyTemporalFilter(DIC3DPPresults& results) {
    if (!config_.smoothTimeLogic) {
        std::cout << "Temporal filtering disabled" << std::endl;
        return;
    }
    
    std::cout << "Applying temporal filter (cutoff: " << config_.filterFreq 
              << " Hz, fs: " << config_.vid_sample_freq << " Hz)..." << std::endl;
    
    // Filter displacement vectors if needed
    for (const auto& plot_field : config_.plotopt) {
        if (plot_field == "DispX" || plot_field == "DispY" || plot_field == "DispZ") {
            if (!results.Disp.DispVec.empty()) {
                std::cout << "  Filtering displacement vector (" << plot_field << ")" << std::endl;
                results.Disp.DispVec = butterworthFilter(
                    results.Disp.DispVec, 
                    config_.filterFreq, 
                    config_.vid_sample_freq);
            }
        } else if (plot_field == "DispMgn") {
            if (!results.Disp.DispMgn.empty()) {
                std::cout << "  Filtering displacement magnitude" << std::endl;
                results.Disp.DispMgn = butterworthFilter(
                    results.Disp.DispMgn, 
                    config_.filterFreq, 
                    config_.vid_sample_freq);
            }
        }
    }
    
    // Filter correlation coefficients
    if (!results.FaceCorrComb.empty()) {
        std::cout << "  Filtering face correlation" << std::endl;
        results.FaceCorrComb = butterworthFilter(
            results.FaceCorrComb, 
            config_.filterFreq, 
            config_.vid_sample_freq);
    }
    
    std::cout << "Temporal filtering complete" << std::endl;
}

std::vector<double> Visualization::applySpatialSmooth(
    const std::vector<double>& face_data,
    const std::vector<int>& faces) {
    
    if (!config_.smoothSpaceLogic) {
        return face_data;
    }
    
    // Simple spatial smoothing using neighboring faces
    // For a more sophisticated implementation, use Gaussian smoothing
    std::vector<double> smoothed = face_data;
    
    // TODO: Implement proper spatial smoothing
    // This is a placeholder - full implementation would require:
    // 1. Build face adjacency graph
    // 2. Apply Gaussian kernel smoothing with parameters n and sigma
    // 3. Handle boundary conditions
    
    return smoothed;
}

std::vector<std::vector<double>> Visualization::extractScalarField(
    const DIC3DPPresults& results, 
    const std::string& field_name) {
    
    std::vector<std::vector<double>> field_data;
    size_t n_frames = results.Points3D.size();
    
    if (n_frames == 0) {
        std::cerr << "Warning: No frames in results" << std::endl;
        return field_data;
    }
    
    // Extract based on field name
    if (field_name == "DispX") {
        // Extract X component from DispVec
        for (size_t i = 0; i < n_frames; ++i) {
            if (i < results.Disp.DispVec.size() && results.Disp.DispVec[i].size() >= 3) {
                std::vector<double> frame_data;
                for (size_t j = 0; j < results.Disp.DispVec[i].size(); j += 3) {
                    frame_data.push_back(results.Disp.DispVec[i][j]);
                }
                field_data.push_back(frame_data);
            }
        }
    } else if (field_name == "DispY") {
        for (size_t i = 0; i < n_frames; ++i) {
            if (i < results.Disp.DispVec.size() && results.Disp.DispVec[i].size() >= 3) {
                std::vector<double> frame_data;
                for (size_t j = 1; j < results.Disp.DispVec[i].size(); j += 3) {
                    frame_data.push_back(results.Disp.DispVec[i][j]);
                }
                field_data.push_back(frame_data);
            }
        }
    } else if (field_name == "DispZ") {
        for (size_t i = 0; i < n_frames; ++i) {
            if (i < results.Disp.DispVec.size() && results.Disp.DispVec[i].size() >= 3) {
                std::vector<double> frame_data;
                for (size_t j = 2; j < results.Disp.DispVec[i].size(); j += 3) {
                    frame_data.push_back(results.Disp.DispVec[i][j]);
                }
                field_data.push_back(frame_data);
            }
        }
    } else if (field_name == "DispMgn") {
        field_data = results.Disp.DispMgn;
    } else if (field_name == "FaceCorrComb") {
        field_data = results.FaceCorrComb;
    } else if (field_name == "Epc1" || field_name == "Epc2") {
        // Compute principal strains
        std::vector<std::vector<double>> Epc1, Epc2;
        // Note: This requires the Deform data to be populated
        // For now, return empty if not available
        std::cout << "Warning: Principal strain computation requires Deform data" << std::endl;
    } else if (field_name == "J") {
        // Jacobian/volume change
        // Note: Requires Deform data
        std::cout << "Warning: J field requires Deform data" << std::endl;
    } else if (field_name == "EShearMax") {
        // Max shear strain
        std::cout << "Warning: Max shear strain requires Deform data" << std::endl;
    }
    
    return field_data;
}

void Visualization::applyCorrelationFilter(
    std::vector<std::vector<double>>& face_data,
    const std::vector<std::vector<double>>& corr_data) {
    
    if (!config_.maxCorrCoeff || corr_data.empty()) {
        return;
    }
    
    std::cout << "Applying correlation filter (max coeff: " 
              << config_.maxCorrCoeff << ")" << std::endl;
    
    for (size_t frame = 0; frame < face_data.size() && frame < corr_data.size(); ++frame) {
        for (size_t i = 0; i < face_data[frame].size() && i < corr_data[frame].size(); ++i) {
            if (corr_data[frame][i] > config_.maxCorrCoeff) {
                face_data[frame][i] = std::numeric_limits<double>::quiet_NaN();
            }
        }
    }
}

void Visualization::applyGapFilter(
    std::vector<std::vector<double>>& face_data,
    const std::vector<int>& FacePairInds) {
    
    if (!config_.gapLogic || FacePairInds.empty()) {
        return;
    }
    
    std::cout << "Applying gap filter (suppressing stitched boundaries)" << std::endl;
    
    // Identify faces at boundaries between pairs and set to NaN
    // This requires analyzing FacePairInds to find transitions
    for (size_t i = 0; i + 1 < FacePairInds.size(); ++i) {
        if (FacePairInds[i] != FacePairInds[i + 1]) {
            // This face is at a boundary
            for (size_t frame = 0; frame < face_data.size(); ++frame) {
                if (i < face_data[frame].size()) {
                    face_data[frame][i] = std::numeric_limits<double>::quiet_NaN();
                }
            }
        }
    }
}

VisData Visualization::prepareVisualizationData(const DIC3DPPresults& results) {
    std::cout << "Preparing visualization data..." << std::endl;
    
    VisData vis_data;
    vis_data.Points3D = results.Points3D;
    vis_data.Faces = results.Faces;
    vis_data.FaceColors = results.FaceCorrComb;
    vis_data.n_frames = results.Points3D.size();
    
    // Extract requested scalar fields
    for (const auto& field_name : config_.plotopt) {
        std::cout << "  Extracting field: " << field_name << std::endl;
        auto field_data = extractScalarField(results, field_name);
        
        if (!field_data.empty()) {
            // Apply filters
            if (!results.FaceCorrComb.empty()) {
                applyCorrelationFilter(field_data, results.FaceCorrComb);
            }
            
            if (!results.FacePairInds.empty()) {
                applyGapFilter(field_data, results.FacePairInds);
            }
            
            // Apply spatial smoothing if enabled
            if (config_.smoothSpaceLogic) {
                for (auto& frame_data : field_data) {
                    frame_data = applySpatialSmooth(frame_data, results.Faces);
                }
            }
            
            vis_data.FaceScalars[field_name] = field_data;
        }
    }
    
    std::cout << "Visualization data prepared (" << vis_data.n_frames 
              << " frames, " << vis_data.FaceScalars.size() << " fields)" << std::endl;
    
    return vis_data;
}

void Visualization::exportData(
    const VisData& vis_data, 
    const std::string& output_path, 
    int frame_idx) {
    
    // Determine which frames to export
    std::vector<int> frames_to_export;
    if (frame_idx >= 0) {
        frames_to_export.push_back(frame_idx);
    } else if (!config_.export_frame_list.empty()) {
        frames_to_export = config_.export_frame_list;
    } else if (config_.export_each_frame) {
        for (size_t i = 0; i < vis_data.n_frames; ++i) {
            frames_to_export.push_back(static_cast<int>(i));
        }
    } else {
        // Export all frames to a single file or last frame only
        frames_to_export.push_back(static_cast<int>(vis_data.n_frames - 1));
    }
    
    // Export each frame
    for (int frame : frames_to_export) {
        if (frame < 0 || frame >= static_cast<int>(vis_data.n_frames)) {
            std::cerr << "Warning: Frame " << frame << " out of range" << std::endl;
            continue;
        }
        
        std::string frame_suffix = config_.export_each_frame ? 
            ("_frame" + std::to_string(frame)) : "";
        
        std::string filepath = output_path + frame_suffix;
        
        switch (export_format_) {
            case ExportFormat::VTK:
                exportFrameVTK(vis_data, filepath + ".vtk", frame);
                break;
            case ExportFormat::PLY:
                exportFramePLY(vis_data, filepath + ".ply", frame);
                break;
            case ExportFormat::CSV:
                exportFrameCSV(vis_data, filepath + ".csv", frame);
                break;
        }
    }
}

void Visualization::exportFrameVTK(
    const VisData& vis_data, 
    const std::string& filepath, 
    int frame_idx) {
    
    std::cout << "Exporting frame " << frame_idx << " to VTK: " << filepath << std::endl;
    
    std::ofstream ofs(filepath);
    if (!ofs) {
        throw std::runtime_error("Cannot open file for writing: " + filepath);
    }
    
    // VTK Legacy format header
    ofs << "# vtk DataFile Version 3.0\n";
    ofs << "DIC 3D Results - Frame " << frame_idx << "\n";
    ofs << "ASCII\n";
    ofs << "DATASET POLYDATA\n";
    
    // Write points
    const auto& pts = vis_data.Points3D[frame_idx];
    size_t n_points = pts.x.size();
    ofs << "POINTS " << n_points << " float\n";
    for (size_t i = 0; i < n_points; ++i) {
        ofs << pts.x[i] << " " << pts.y[i] << " " << pts.z[i] << "\n";
    }
    
    // Write polygons (triangles)
    size_t n_faces = vis_data.Faces.size() / 3;
    ofs << "\nPOLYGONS " << n_faces << " " << (n_faces * 4) << "\n";
    for (size_t i = 0; i < n_faces; ++i) {
        ofs << "3 " 
            << vis_data.Faces[i * 3] << " "
            << vis_data.Faces[i * 3 + 1] << " "
            << vis_data.Faces[i * 3 + 2] << "\n";
    }
    
    // Write cell data (face scalars)
    if (!vis_data.FaceScalars.empty()) {
        ofs << "\nCELL_DATA " << n_faces << "\n";
        
        for (const auto& [field_name, field_data] : vis_data.FaceScalars) {
            if (frame_idx >= static_cast<int>(field_data.size())) continue;
            
            const auto& frame_data = field_data[frame_idx];
            if (frame_data.size() != n_faces) {
                std::cerr << "Warning: Field " << field_name 
                          << " size mismatch (" << frame_data.size() 
                          << " vs " << n_faces << ")" << std::endl;
                continue;
            }
            
            ofs << "\nSCALARS " << field_name << " float 1\n";
            ofs << "LOOKUP_TABLE default\n";
            for (double val : frame_data) {
                ofs << val << "\n";
            }
        }
    }
    
    ofs.close();
    std::cout << "  VTK export complete: " << n_points << " points, " 
              << n_faces << " faces" << std::endl;
}

void Visualization::exportFramePLY(
    const VisData& vis_data, 
    const std::string& filepath, 
    int frame_idx) {
    
    std::cout << "Exporting frame " << frame_idx << " to PLY: " << filepath << std::endl;
    
    std::ofstream ofs(filepath);
    if (!ofs) {
        throw std::runtime_error("Cannot open file for writing: " + filepath);
    }
    
    const auto& pts = vis_data.Points3D[frame_idx];
    size_t n_points = pts.x.size();
    size_t n_faces = vis_data.Faces.size() / 3;
    
    // PLY header
    ofs << "ply\n";
    ofs << "format ascii 1.0\n";
    ofs << "comment DIC 3D Results - Frame " << frame_idx << "\n";
    ofs << "element vertex " << n_points << "\n";
    ofs << "property float x\n";
    ofs << "property float y\n";
    ofs << "property float z\n";
    ofs << "element face " << n_faces << "\n";
    ofs << "property list uchar int vertex_indices\n";
    ofs << "end_header\n";
    
    // Write vertices
    for (size_t i = 0; i < n_points; ++i) {
        ofs << pts.x[i] << " " << pts.y[i] << " " << pts.z[i] << "\n";
    }
    
    // Write faces
    for (size_t i = 0; i < n_faces; ++i) {
        ofs << "3 " 
            << vis_data.Faces[i * 3] << " "
            << vis_data.Faces[i * 3 + 1] << " "
            << vis_data.Faces[i * 3 + 2] << "\n";
    }
    
    ofs.close();
    std::cout << "  PLY export complete" << std::endl;
}

void Visualization::exportFrameCSV(
    const VisData& vis_data, 
    const std::string& filepath, 
    int frame_idx) {
    
    std::cout << "Exporting frame " << frame_idx << " to CSV: " << filepath << std::endl;
    
    std::ofstream ofs(filepath);
    if (!ofs) {
        throw std::runtime_error("Cannot open file for writing: " + filepath);
    }
    
    const auto& pts = vis_data.Points3D[frame_idx];
    size_t n_points = pts.x.size();
    
    // CSV header
    ofs << "point_id,x,y,z";
    for (const auto& [field_name, _] : vis_data.FaceScalars) {
        ofs << "," << field_name;
    }
    ofs << "\n";
    
    // Write data (point-based)
    for (size_t i = 0; i < n_points; ++i) {
        ofs << i << "," << pts.x[i] << "," << pts.y[i] << "," << pts.z[i];
        
        // Add scalar fields (if point-based conversion available)
        for (const auto& [field_name, field_data] : vis_data.FaceScalars) {
            // For simplicity, write NaN for now
            // Full implementation would interpolate face data to points
            ofs << ",NaN";
        }
        ofs << "\n";
    }
    
    ofs.close();
    std::cout << "  CSV export complete" << std::endl;
}

void Visualization::generateSummaryStats(
    const DIC3DPPresults& results, 
    const std::string& output_path) {
    
    std::cout << "Generating summary statistics..." << std::endl;
    
    std::ofstream ofs(output_path);
    if (!ofs) {
        throw std::runtime_error("Cannot open file for writing: " + output_path);
    }
    
    ofs << "DIC 3D Post-Processing Results Summary\n";
    ofs << "========================================\n\n";
    
    // Basic info
    ofs << "Number of frames: " << results.Points3D.size() << "\n";
    if (!results.Points3D.empty()) {
        ofs << "Number of points per frame: " << results.Points3D[0].x.size() << "\n";
    }
    ofs << "Number of faces: " << (results.Faces.size() / 3) << "\n";
    ofs << "Deformation type: " << results.deftype << "\n";
    ofs << "\n";
    
    // Field statistics
    if (!results.Disp.DispMgn.empty() && !results.Disp.DispMgn[0].empty()) {
        ofs << "Displacement Statistics:\n";
        ofs << "------------------------\n";
        
        for (size_t frame = 0; frame < results.Disp.DispMgn.size(); ++frame) {
            const auto& disp = results.Disp.DispMgn[frame];
            
            double min_val = *std::min_element(disp.begin(), disp.end());
            double max_val = *std::max_element(disp.begin(), disp.end());
            double mean_val = std::accumulate(disp.begin(), disp.end(), 0.0) / disp.size();
            
            ofs << std::setprecision(6) << std::fixed;
            ofs << "  Frame " << frame << ": "
                << "min=" << min_val << ", "
                << "max=" << max_val << ", "
                << "mean=" << mean_val << "\n";
        }
        ofs << "\n";
    }
    
    // Correlation statistics
    if (!results.FaceCorrComb.empty() && !results.FaceCorrComb[0].empty()) {
        ofs << "Correlation Coefficient Statistics:\n";
        ofs << "------------------------------------\n";
        
        for (size_t frame = 0; frame < results.FaceCorrComb.size(); ++frame) {
            const auto& corr = results.FaceCorrComb[frame];
            
            double min_val = *std::min_element(corr.begin(), corr.end());
            double max_val = *std::max_element(corr.begin(), corr.end());
            double mean_val = std::accumulate(corr.begin(), corr.end(), 0.0) / corr.size();
            
            ofs << std::setprecision(6) << std::fixed;
            ofs << "  Frame " << frame << ": "
                << "min=" << min_val << ", "
                << "max=" << max_val << ", "
                << "mean=" << mean_val << "\n";
        }
        ofs << "\n";
    }
    
    ofs.close();
    std::cout << "Summary statistics written to: " << output_path << std::endl;
}

void Visualization::printTrialInfo(const DIC3DPPresults& results) {
    std::cout << "\n========================================\n";
    std::cout << "DIC 3D Post-Processing Results Info\n";
    std::cout << "========================================\n";
    std::cout << "Subject: " << config_.subject_id << "\n";
    std::cout << "Phase: " << config_.phase_id << "\n";
    std::cout << "Material: " << config_.material << "\n";
    std::cout << "Number of frames: " << results.Points3D.size() << "\n";
    std::cout << "Number of pairs: " << config_.num_pair << "\n";
    std::cout << "File version: " << config_.fileversion << "\n";
    std::cout << "========================================\n\n";
}

std::vector<std::vector<double>> Visualization::butterworthFilter(
    const std::vector<std::vector<double>>& data,
    double cutoff_freq,
    double sample_freq) {
    
    // Simple placeholder implementation
    // Full implementation would require proper Butterworth filter design
    // Consider using a signal processing library like DSP++ or Eigen for this
    
    std::cout << "  Note: Butterworth filter not fully implemented, returning unfiltered data" << std::endl;
    
    // For now, return the data unchanged
    // TODO: Implement proper Butterworth low-pass filter
    return data;
}

} // namespace cppxdic
