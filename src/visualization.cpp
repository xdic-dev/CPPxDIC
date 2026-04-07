/**
 * Implementation of visualization module for DIC results
 */

#include "visualization.h"
#include "dic_structures.h"
#include "strain_computation.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <functional>
#include <filesystem>
#include <map>
#include <set>
#include <queue>
#include <array>

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

/**
 * Build face adjacency map from triangle connectivity
 * For each face, find all faces that share at least one vertex
 * @param faces Flattened triangle indices (size = n_faces * 3)
 * @param n_faces Number of faces
 * @return Adjacency list: adj[face_idx] = vector of adjacent face indices
 */
static std::vector<std::vector<int>> buildFaceAdjacency(
    const std::vector<int>& faces, 
    size_t n_faces) {
    
    // Build vertex-to-face map
    std::map<int, std::vector<int>> vertex_to_faces;
    for (size_t f = 0; f < n_faces; ++f) {
        int v0 = faces[f * 3];
        int v1 = faces[f * 3 + 1];
        int v2 = faces[f * 3 + 2];
        vertex_to_faces[v0].push_back(static_cast<int>(f));
        vertex_to_faces[v1].push_back(static_cast<int>(f));
        vertex_to_faces[v2].push_back(static_cast<int>(f));
    }
    
    // Build face adjacency (faces sharing at least one vertex)
    std::vector<std::vector<int>> adjacency(n_faces);
    for (size_t f = 0; f < n_faces; ++f) {
        std::set<int> neighbors;
        int v0 = faces[f * 3];
        int v1 = faces[f * 3 + 1];
        int v2 = faces[f * 3 + 2];
        
        for (int adj_f : vertex_to_faces[v0]) {
            if (adj_f != static_cast<int>(f)) neighbors.insert(adj_f);
        }
        for (int adj_f : vertex_to_faces[v1]) {
            if (adj_f != static_cast<int>(f)) neighbors.insert(adj_f);
        }
        for (int adj_f : vertex_to_faces[v2]) {
            if (adj_f != static_cast<int>(f)) neighbors.insert(adj_f);
        }
        
        adjacency[f] = std::vector<int>(neighbors.begin(), neighbors.end());
    }
    
    return adjacency;
}

/**
 * Compute face centroids from vertices and faces
 * @param points 3D points for the current frame
 * @param faces Flattened triangle indices
 * @param n_faces Number of faces
 * @return Vector of centroids (x, y, z) for each face
 */
static std::vector<std::array<double, 3>> computeFaceCentroids(
    const Point3DFrame& points,
    const std::vector<int>& faces,
    size_t n_faces) {
    
    std::vector<std::array<double, 3>> centroids(n_faces);
    
    for (size_t f = 0; f < n_faces; ++f) {
        int v0 = faces[f * 3];
        int v1 = faces[f * 3 + 1];
        int v2 = faces[f * 3 + 2];
        
        centroids[f][0] = (points.x[v0] + points.x[v1] + points.x[v2]) / 3.0;
        centroids[f][1] = (points.y[v0] + points.y[v1] + points.y[v2]) / 3.0;
        centroids[f][2] = (points.z[v0] + points.z[v1] + points.z[v2]) / 3.0;
    }
    
    return centroids;
}

/**
 * Compute average resolution (spacing) of face centroids
 * @param centroids Face centroid positions
 * @param adjacency Face adjacency list
 * @return Average distance between adjacent face centroids
 */
static double computeAverageResolution(
    const std::vector<std::array<double, 3>>& centroids,
    const std::vector<std::vector<int>>& adjacency) {
    
    double sum_dist = 0.0;
    size_t count = 0;
    
    for (size_t f = 0; f < centroids.size(); ++f) {
        for (int adj_f : adjacency[f]) {
            double dx = centroids[f][0] - centroids[adj_f][0];
            double dy = centroids[f][1] - centroids[adj_f][1];
            double dz = centroids[f][2] - centroids[adj_f][2];
            sum_dist += std::sqrt(dx*dx + dy*dy + dz*dz);
            count++;
        }
    }
    
    return (count > 0) ? (sum_dist / count) : 1.0;
}

/**
 * Find k-nearest neighbors for each face using BFS on adjacency graph
 * @param adjacency Face adjacency list
 * @param k Number of neighbors to find
 * @return For each face: vector of (neighbor_idx, distance_in_hops)
 */
static std::vector<std::vector<std::pair<int, int>>> findKNearestByHops(
    const std::vector<std::vector<int>>& adjacency,
    int k) {
    
    size_t n_faces = adjacency.size();
    std::vector<std::vector<std::pair<int, int>>> neighbors(n_faces);
    
    for (size_t f = 0; f < n_faces; ++f) {
        // BFS to find k nearest neighbors by graph distance
        std::vector<int> dist(n_faces, -1);
        std::queue<int> queue;
        queue.push(static_cast<int>(f));
        dist[f] = 0;
        
        std::vector<std::pair<int, int>> found;  // (face_idx, hop_distance)
        found.push_back({static_cast<int>(f), 0});
        
        while (!queue.empty() && static_cast<int>(found.size()) < k + 1) {
            int curr = queue.front();
            queue.pop();
            
            for (int adj : adjacency[curr]) {
                if (dist[adj] < 0) {
                    dist[adj] = dist[curr] + 1;
                    queue.push(adj);
                    found.push_back({adj, dist[adj]});
                    if (static_cast<int>(found.size()) >= k + 1) break;
                }
            }
        }
        
        neighbors[f] = found;
    }
    
    return neighbors;
}

std::vector<double> Visualization::applySpatialSmooth(
    const std::vector<double>& face_data,
    const std::vector<int>& faces) {
    
    if (!config_.smoothSpaceLogic) {
        return face_data;
    }
    
    size_t n_faces = face_data.size();
    if (n_faces == 0 || faces.empty()) {
        return face_data;
    }
    
    // Verify faces array size
    if (faces.size() < n_faces * 3) {
        std::cerr << "  Warning: Faces array too small for spatial smoothing" << std::endl;
        return face_data;
    }
    
    int num_neighbors = config_.smoothPar_n;      // Number of neighbors (default: 30)
    double sigma = config_.smoothPar_sigma;        // Gaussian sigma (default: 2.0)
    
    std::cout << "  Applying spatial smoothing (n=" << num_neighbors 
              << ", sigma=" << sigma << ")..." << std::endl;
    
    // Build face adjacency graph
    auto adjacency = buildFaceAdjacency(faces, n_faces);
    
    // Find k-nearest neighbors by graph distance (hops)
    auto neighbors = findKNearestByHops(adjacency, num_neighbors);
    
    // Compute Gaussian weights based on hop distance
    // W = exp(-(hop_dist^2) / sigma^2)
    // This matches MATLAB's gaussmf(0:N, [sigma, 0])
    std::vector<double> smoothed(n_faces);
    
    for (size_t f = 0; f < n_faces; ++f) {
        double weighted_sum = 0.0;
        double weight_sum = 0.0;
        
        for (const auto& [neighbor_idx, hop_dist] : neighbors[f]) {
            double val = face_data[neighbor_idx];
            
            // Skip NaN values
            if (std::isnan(val)) continue;
            
            // Gaussian weight based on hop distance
            double weight = std::exp(-(static_cast<double>(hop_dist * hop_dist)) / (sigma * sigma));
            
            weighted_sum += weight * val;
            weight_sum += weight;
        }
        
        if (weight_sum > 0) {
            smoothed[f] = weighted_sum / weight_sum;
        } else {
            smoothed[f] = face_data[f];  // Keep original if no valid neighbors
        }
    }
    
    std::cout << "  Spatial smoothing complete" << std::endl;
    
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
    } else if (!results.deform_full.frames.empty()) {
        // Extract from deform_full (FrameDeformationResult) — all 15 scalar fields
        size_t df_frames = results.deform_full.n_frames;
        
        // Map field name to member accessor
        std::function<const std::vector<double>&(const DeformationResult&)> accessor;
        if      (field_name == "Epc1")      accessor = [](const DeformationResult& d) -> const std::vector<double>& { return d.Epc1; };
        else if (field_name == "Epc2")      accessor = [](const DeformationResult& d) -> const std::vector<double>& { return d.Epc2; };
        else if (field_name == "epc1")      accessor = [](const DeformationResult& d) -> const std::vector<double>& { return d.epc1; };
        else if (field_name == "epc2")      accessor = [](const DeformationResult& d) -> const std::vector<double>& { return d.epc2; };
        else if (field_name == "J")         accessor = [](const DeformationResult& d) -> const std::vector<double>& { return d.J; };
        else if (field_name == "EShearMax") accessor = [](const DeformationResult& d) -> const std::vector<double>& { return d.EShearMax; };
        else if (field_name == "eShearMax") accessor = [](const DeformationResult& d) -> const std::vector<double>& { return d.eShearMax; };
        else if (field_name == "Emgn")      accessor = [](const DeformationResult& d) -> const std::vector<double>& { return d.Emgn; };
        else if (field_name == "emgn")      accessor = [](const DeformationResult& d) -> const std::vector<double>& { return d.emgn; };
        else if (field_name == "Eeq")       accessor = [](const DeformationResult& d) -> const std::vector<double>& { return d.Eeq; };
        else if (field_name == "eeq")       accessor = [](const DeformationResult& d) -> const std::vector<double>& { return d.eeq; };
        else if (field_name == "Lamda1")    accessor = [](const DeformationResult& d) -> const std::vector<double>& { return d.Lamda1; };
        else if (field_name == "Lamda2")    accessor = [](const DeformationResult& d) -> const std::vector<double>& { return d.Lamda2; };
        else if (field_name == "Area")      accessor = [](const DeformationResult& d) -> const std::vector<double>& { return d.Area; };
        else if (field_name == "Dnorm")     accessor = [](const DeformationResult& d) -> const std::vector<double>& { return d.Dnorm; };
        
        if (accessor) {
            for (size_t i = 0; i < df_frames && i < results.deform_full.frames.size(); ++i) {
                field_data.push_back(accessor(results.deform_full.frames[i]));
            }
        } else {
            std::cerr << "Warning: Unknown deformation field '" << field_name << "'" << std::endl;
        }
    } else {
        std::cerr << "Warning: No deformation data available for field '" << field_name << "'" << std::endl;
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
    
    // MATLAB anim8_rewrited.m line 336: FCnow(data.FacePairInds==3)=NaN
    // FacePairInds==3 identifies faces at the stitched boundary between pairs
    for (size_t i = 0; i < FacePairInds.size(); ++i) {
        if (FacePairInds[i] == 3) {
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
    
    // Helper lambda to compute statistics filtering out NaN/Inf values
    auto computeStats = [](const std::vector<double>& data, 
                          double& min_val, double& max_val, double& mean_val,
                          size_t& num_valid, size_t& num_nan, size_t& num_zeros) {
        min_val = std::numeric_limits<double>::infinity();
        max_val = -std::numeric_limits<double>::infinity();
        mean_val = 0.0;
        num_valid = 0;
        num_nan = 0;
        num_zeros = 0;
        
        double sum = 0.0;
        for (const auto& val : data) {
            if (std::isnan(val)) {
                num_nan++;
            } else if (std::isinf(val)) {
                // Skip infinities
            } else {
                num_valid++;
                sum += val;
                min_val = std::min(min_val, val);
                max_val = std::max(max_val, val);
                if (std::abs(val) < 1e-10) {
                    num_zeros++;
                }
            }
        }
        
        if (num_valid > 0) {
            mean_val = sum / num_valid;
        } else {
            min_val = std::numeric_limits<double>::quiet_NaN();
            max_val = std::numeric_limits<double>::quiet_NaN();
            mean_val = std::numeric_limits<double>::quiet_NaN();
        }
    };
    
    // Field statistics
    if (!results.Disp.DispMgn.empty() && !results.Disp.DispMgn[0].empty()) {
        ofs << "Displacement Statistics:\n";
        ofs << "------------------------\n";
        
        for (size_t frame = 0; frame < results.Disp.DispMgn.size(); ++frame) {
            const auto& disp = results.Disp.DispMgn[frame];
            
            double min_val, max_val, mean_val;
            size_t num_valid, num_nan, num_zeros;
            computeStats(disp, min_val, max_val, mean_val, num_valid, num_nan, num_zeros);
            
            ofs << std::setprecision(6) << std::fixed;
            ofs << "  Frame " << frame << ": "
                << "min=" << min_val << ", "
                << "max=" << max_val << ", "
                << "mean=" << mean_val;
            
            if (num_nan > 0 || num_zeros > 0) {
                ofs << " (valid=" << num_valid << "/" << disp.size();
                if (num_nan > 0) {
                    ofs << ", NaN=" << num_nan;
                }
                if (num_zeros > 0) {
                    ofs << ", zeros=" << num_zeros;
                }
                ofs << ")";
            }
            ofs << "\n";
        }
        ofs << "\n";
    } else {
        ofs << "Displacement Statistics:\n";
        ofs << "------------------------\n";
        ofs << "  No displacement data available\n\n";
    }
    
    // Correlation statistics
    if (!results.FaceCorrComb.empty() && !results.FaceCorrComb[0].empty()) {
        ofs << "Correlation Coefficient Statistics:\n";
        ofs << "------------------------------------\n";
        
        for (size_t frame = 0; frame < results.FaceCorrComb.size(); ++frame) {
            const auto& corr = results.FaceCorrComb[frame];
            
            double min_val, max_val, mean_val;
            size_t num_valid, num_nan, num_zeros;
            computeStats(corr, min_val, max_val, mean_val, num_valid, num_nan, num_zeros);
            
            ofs << std::setprecision(6) << std::fixed;
            ofs << "  Frame " << frame << ": "
                << "min=" << min_val << ", "
                << "max=" << max_val << ", "
                << "mean=" << mean_val;
            
            if (num_nan > 0 || num_zeros > 0) {
                ofs << " (valid=" << num_valid << "/" << corr.size();
                if (num_nan > 0) {
                    ofs << ", NaN=" << num_nan;
                }
                if (num_zeros > 0) {
                    ofs << ", zeros=" << num_zeros;
                }
                ofs << ")";
            }
            ofs << "\n";
        }
        ofs << "\n";
    } else {
        ofs << "Correlation Coefficient Statistics:\n";
        ofs << "------------------------------------\n";
        ofs << "  No correlation data available\n\n";
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

/**
 * Design 4th order Butterworth low-pass filter coefficients
 * Equivalent to MATLAB's [B,A] = butter(4, Wn)
 * @param Wn Normalized cutoff frequency (0 to 1, where 1 = Nyquist)
 * @param b Output numerator coefficients (size 5 for 4th order)
 * @param a Output denominator coefficients (size 5 for 4th order)
 */
static void designButterworth4(double Wn, std::vector<double>& b, std::vector<double>& a) {
    // Pre-warp the cutoff frequency for bilinear transform
    double Wn_clamped = std::max(0.001, std::min(0.999, Wn));
    double Wp = std::tan(M_PI * Wn_clamped / 2.0);
    
    // Scale by cutoff frequency
    double Wp2 = Wp * Wp;
    double Wp4 = Wp2 * Wp2;
    
    // Pre-computed coefficients for 4th order Butterworth analog prototype
    // Based on analog prototype: H(s) = 1 / (s^4 + 2.6131*s^3 + 3.4142*s^2 + 2.6131*s + 1)
    double a1_analog = 2.6131;  // 2*(cos(5π/8) + cos(7π/8)) with sign
    double a2_analog = 3.4142;  // 2 + 2*cos(5π/8)*cos(7π/8) + ...
    double a3_analog = 2.6131;
    double a4_analog = 1.0;
    
    // Bilinear transform with frequency pre-warping
    // s = Wp * (z-1)/(z+1)
    double Wp1 = Wp;
    double Wp3 = Wp2 * Wp;
    
    // Denominator coefficients after bilinear transform
    double d0 = Wp4 + a1_analog*Wp3 + a2_analog*Wp2 + a3_analog*Wp1 + a4_analog;
    double d1 = 4*Wp4 + 2*a1_analog*Wp3 - 2*a3_analog*Wp1 - 4*a4_analog;
    double d2 = 6*Wp4 - 2*a2_analog*Wp2 + 6*a4_analog;
    double d3 = 4*Wp4 - 2*a1_analog*Wp3 + 2*a3_analog*Wp1 - 4*a4_analog;
    double d4 = Wp4 - a1_analog*Wp3 + a2_analog*Wp2 - a3_analog*Wp1 + a4_analog;
    
    // Normalize by d0
    a.resize(5);
    a[0] = 1.0;
    a[1] = d1 / d0;
    a[2] = d2 / d0;
    a[3] = d3 / d0;
    a[4] = d4 / d0;
    
    // Numerator: all-pole filter, so numerator is Wp^4 * (1 + z^-1)^4
    // = Wp^4 * (1 + 4*z^-1 + 6*z^-2 + 4*z^-3 + z^-4)
    double gain = Wp4 / d0;
    b.resize(5);
    b[0] = gain;
    b[1] = 4 * gain;
    b[2] = 6 * gain;
    b[3] = 4 * gain;
    b[4] = gain;
}

/**
 * Apply zero-phase filtering (equivalent to MATLAB's filtfilt)
 * Filters forward and backward to eliminate phase distortion
 * @param b Numerator coefficients
 * @param a Denominator coefficients
 * @param x Input signal
 * @return Filtered signal
 */
static std::vector<double> filtfilt(const std::vector<double>& b, 
                                     const std::vector<double>& a,
                                     const std::vector<double>& x) {
    if (x.empty()) return x;
    
    size_t n = x.size();
    size_t order = b.size() - 1;
    
    // Pad length (3 times filter order, similar to MATLAB)
    size_t npad = std::min(3 * order, n - 1);
    
    // Create padded signal with reflected boundaries
    std::vector<double> xpad(n + 2 * npad);
    
    // Reflect at beginning: 2*x[0] - x[npad], ..., 2*x[0] - x[1]
    for (size_t i = 0; i < npad; ++i) {
        xpad[i] = 2.0 * x[0] - x[npad - i];
    }
    // Copy original signal
    for (size_t i = 0; i < n; ++i) {
        xpad[npad + i] = x[i];
    }
    // Reflect at end: 2*x[n-1] - x[n-2], ..., 2*x[n-1] - x[n-1-npad]
    for (size_t i = 0; i < npad; ++i) {
        xpad[npad + n + i] = 2.0 * x[n - 1] - x[n - 2 - i];
    }
    
    // Forward filter (Direct Form II Transposed)
    auto filter = [&](const std::vector<double>& input) -> std::vector<double> {
        std::vector<double> output(input.size());
        std::vector<double> z(order + 1, 0.0);  // State variables
        
        for (size_t i = 0; i < input.size(); ++i) {
            double y = b[0] * input[i] + z[0];
            for (size_t j = 0; j < order; ++j) {
                z[j] = b[j + 1] * input[i] - a[j + 1] * y + z[j + 1];
            }
            output[i] = y;
        }
        return output;
    };
    
    // Forward pass
    std::vector<double> y_fwd = filter(xpad);
    
    // Reverse the signal
    std::reverse(y_fwd.begin(), y_fwd.end());
    
    // Backward pass
    std::vector<double> y_bwd = filter(y_fwd);
    
    // Reverse back and extract original length
    std::reverse(y_bwd.begin(), y_bwd.end());
    
    // Extract the central portion (remove padding)
    std::vector<double> result(n);
    for (size_t i = 0; i < n; ++i) {
        result[i] = y_bwd[npad + i];
    }
    
    return result;
}

/**
 * Fill missing values (NaN) with linear interpolation
 * Equivalent to MATLAB's fillmissing(d,'linear',1,'EndValues','nearest')
 */
static void fillMissingLinear(std::vector<double>& data) {
    size_t n = data.size();
    if (n == 0) return;
    
    // Find first valid value for start
    size_t first_valid = 0;
    while (first_valid < n && std::isnan(data[first_valid])) {
        first_valid++;
    }
    if (first_valid == n) return;  // All NaN
    
    // Fill leading NaNs with first valid value
    for (size_t i = 0; i < first_valid; ++i) {
        data[i] = data[first_valid];
    }
    
    // Find last valid value for end
    size_t last_valid = n - 1;
    while (last_valid > 0 && std::isnan(data[last_valid])) {
        last_valid--;
    }
    
    // Fill trailing NaNs with last valid value
    for (size_t i = last_valid + 1; i < n; ++i) {
        data[i] = data[last_valid];
    }
    
    // Linear interpolation for interior NaNs
    size_t i = first_valid;
    while (i < last_valid) {
        if (std::isnan(data[i])) {
            // Find next valid value
            size_t j = i + 1;
            while (j < n && std::isnan(data[j])) {
                j++;
            }
            // Linear interpolate between i-1 and j
            double v0 = data[i - 1];
            double v1 = data[j];
            double span = static_cast<double>(j - i + 1);
            for (size_t k = i; k < j; ++k) {
                double t = static_cast<double>(k - i + 1) / span;
                data[k] = v0 + t * (v1 - v0);
            }
            i = j;
        } else {
            i++;
        }
    }
}

std::vector<std::vector<double>> Visualization::butterworthFilter(
    const std::vector<std::vector<double>>& data,
    double cutoff_freq,
    double sample_freq) {
    
    if (data.empty() || data[0].empty()) {
        return data;
    }
    
    size_t n_frames = data.size();
    size_t n_points = data[0].size();
    
    // Validate frequencies
    double nyquist = sample_freq / 2.0;
    if (cutoff_freq <= 0 || cutoff_freq >= nyquist) {
        std::cerr << "  Warning: Invalid cutoff frequency (" << cutoff_freq 
                  << " Hz), must be between 0 and Nyquist (" << nyquist 
                  << " Hz). Returning unfiltered data." << std::endl;
        return data;
    }
    
    // Normalized cutoff frequency (0 to 1, where 1 = Nyquist)
    double Wn = cutoff_freq / nyquist;
    
    // Design 4th order Butterworth filter (matching MATLAB's butter(4, Wn))
    std::vector<double> b, a;
    designButterworth4(Wn, b, a);
    
    std::cout << "  Applying 4th order Butterworth filter (fc=" << cutoff_freq 
              << " Hz, fs=" << sample_freq << " Hz, Wn=" << Wn << ")" << std::endl;
    
    // Transpose data: from [frame][point] to [point][frame] for temporal filtering
    std::vector<std::vector<double>> transposed(n_points, std::vector<double>(n_frames));
    for (size_t f = 0; f < n_frames; ++f) {
        for (size_t p = 0; p < n_points && p < data[f].size(); ++p) {
            transposed[p][f] = data[f][p];
        }
    }
    
    // Filter each point's time series
    size_t filtered_count = 0;
    for (size_t p = 0; p < n_points; ++p) {
        // Check if this point has valid data (not all NaN)
        bool has_valid = false;
        for (size_t f = 0; f < n_frames; ++f) {
            if (!std::isnan(transposed[p][f])) {
                has_valid = true;
                break;
            }
        }
        
        if (has_valid) {
            // Fill missing values with linear interpolation
            fillMissingLinear(transposed[p]);
            
            // Apply zero-phase Butterworth filter
            transposed[p] = filtfilt(b, a, transposed[p]);
            filtered_count++;
        }
    }
    
    // Transpose back: from [point][frame] to [frame][point]
    std::vector<std::vector<double>> result(n_frames, std::vector<double>(n_points));
    for (size_t f = 0; f < n_frames; ++f) {
        for (size_t p = 0; p < n_points; ++p) {
            result[f][p] = transposed[p][f];
        }
    }
    
    std::cout << "  Filtered " << filtered_count << "/" << n_points 
              << " point time series" << std::endl;
    
    return result;
}

cv::Vec3b Visualization::applyJetColormap(double val) {
    // Clamp to [0, 1]
    val = std::max(0.0, std::min(1.0, val));
    
    // Use OpenCV's built-in jet colormap via LUT
    cv::Mat gray(1, 1, CV_8UC1, cv::Scalar(static_cast<uchar>(val * 255)));
    cv::Mat color;
    cv::applyColorMap(gray, color, cv::COLORMAP_JET);
    return color.at<cv::Vec3b>(0, 0);
}

cv::Mat Visualization::renderFrame(const VisData& vis_data, int frame_idx,
                                    const std::string& field_name,
                                    double data_min, double data_max,
                                    int width, int height) {
    cv::Mat img(height, width, CV_8UC3, cv::Scalar(255, 255, 255)); // white background
    
    const auto& pts = vis_data.Points3D[frame_idx];
    size_t n_points = pts.x.size();
    size_t n_faces = vis_data.Faces.size() / 3;
    
    if (n_points == 0 || n_faces == 0) return img;
    
    // Get scalar data for this frame (if available)
    const std::vector<double>* scalar_data = nullptr;
    if (!field_name.empty()) {
        auto it = vis_data.FaceScalars.find(field_name);
        if (it != vis_data.FaceScalars.end() && 
            frame_idx < static_cast<int>(it->second.size())) {
            scalar_data = &it->second[frame_idx];
        }
    }
    
    // Compute bounding box for orthographic projection (skip NaN/Inf)
    double xmin = std::numeric_limits<double>::infinity();
    double xmax = -std::numeric_limits<double>::infinity();
    double ymin = std::numeric_limits<double>::infinity();
    double ymax = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < n_points; ++i) {
        if (std::isfinite(pts.x[i]) && std::isfinite(pts.y[i])) {
            xmin = std::min(xmin, pts.x[i]);
            xmax = std::max(xmax, pts.x[i]);
            ymin = std::min(ymin, pts.y[i]);
            ymax = std::max(ymax, pts.y[i]);
        }
    }
    
    double range_x = xmax - xmin;
    double range_y = ymax - ymin;
    if (range_x < 1e-10) range_x = 1.0;
    if (range_y < 1e-10) range_y = 1.0;
    
    // Fit mesh into image with margin
    int margin = 40;
    int draw_w = width - 2 * margin;
    int draw_h = height - 2 * margin - 40; // extra space for colorbar
    double scale = std::min(draw_w / range_x, draw_h / range_y);
    double cx = (xmin + xmax) / 2.0;
    double cy = (ymin + ymax) / 2.0;
    
    // Project 3D points to 2D (orthographic: use X and Y, ignore Z for now)
    auto project = [&](size_t idx) -> cv::Point {
        int px = margin + static_cast<int>((pts.x[idx] - cx) * scale + draw_w / 2.0);
        int py = margin + static_cast<int>((cy - pts.y[idx]) * scale + draw_h / 2.0); // flip Y
        return cv::Point(px, py);
    };
    
    // Helper: check if a vertex has valid (finite) coordinates
    auto vertex_valid = [&](int v) -> bool {
        return v >= 0 && v < static_cast<int>(n_points) &&
               std::isfinite(pts.x[v]) && std::isfinite(pts.y[v]) && std::isfinite(pts.z[v]);
    };
    
    // Sort faces by average Z (painter's algorithm: draw far faces first)
    // Skip faces with any invalid vertex (NaN/Inf/out-of-bounds)
    struct FaceZ { size_t idx; double z; };
    std::vector<FaceZ> sorted_faces;
    sorted_faces.reserve(n_faces);
    for (size_t i = 0; i < n_faces; ++i) {
        int v0 = vis_data.Faces[i * 3];
        int v1 = vis_data.Faces[i * 3 + 1];
        int v2 = vis_data.Faces[i * 3 + 2];
        if (!vertex_valid(v0) || !vertex_valid(v1) || !vertex_valid(v2)) continue;
        double avg_z = (pts.z[v0] + pts.z[v1] + pts.z[v2]) / 3.0;
        sorted_faces.push_back({i, avg_z});
    }
    std::sort(sorted_faces.begin(), sorted_faces.end(),
              [](const FaceZ& a, const FaceZ& b) { return a.z < b.z; });
    
    // Draw triangles
    double data_range = data_max - data_min;
    if (data_range < 1e-15) data_range = 1.0;
    
    for (const auto& fz : sorted_faces) {
        size_t fi = fz.idx;
        int v0 = vis_data.Faces[fi * 3];
        int v1 = vis_data.Faces[fi * 3 + 1];
        int v2 = vis_data.Faces[fi * 3 + 2];
        
        // Vertices are already validated during sorting, so no bounds check needed
        
        // Skip faces with NaN scalar data (matching MATLAB's gpatch behavior)
        if (scalar_data && fi < scalar_data->size() && std::isnan((*scalar_data)[fi])) {
            continue;
        }
        
        cv::Point tri[3] = { project(v0), project(v1), project(v2) };
        
        // Skip degenerate projected triangles (all vertices same pixel)
        if (tri[0] == tri[1] && tri[1] == tri[2]) continue;
        
        // Determine face color
        cv::Scalar color(200, 200, 200); // default gray
        if (scalar_data && fi < scalar_data->size()) {
            double val = (*scalar_data)[fi];
            if (!std::isnan(val)) {
                double norm = (val - data_min) / data_range;
                cv::Vec3b c = applyJetColormap(norm);
                color = cv::Scalar(c[0], c[1], c[2]);
            }
        }
        
        cv::fillConvexPoly(img, tri, 3, color);
        // Draw wireframe edges
        cv::line(img, tri[0], tri[1], cv::Scalar(60, 60, 60), 1, cv::LINE_AA);
        cv::line(img, tri[1], tri[2], cv::Scalar(60, 60, 60), 1, cv::LINE_AA);
        cv::line(img, tri[2], tri[0], cv::Scalar(60, 60, 60), 1, cv::LINE_AA);
    }
    
    // Draw colorbar at bottom
    if (scalar_data) {
        int cb_x = margin;
        int cb_y = height - 35;
        int cb_w = width - 2 * margin;
        int cb_h = 15;
        for (int x = 0; x < cb_w; ++x) {
            double norm = static_cast<double>(x) / cb_w;
            cv::Vec3b c = applyJetColormap(norm);
            cv::line(img, cv::Point(cb_x + x, cb_y), cv::Point(cb_x + x, cb_y + cb_h),
                     cv::Scalar(c[0], c[1], c[2]));
        }
        cv::rectangle(img, cv::Point(cb_x, cb_y), cv::Point(cb_x + cb_w, cb_y + cb_h),
                       cv::Scalar(0, 0, 0), 1);
        
        // Labels
        std::ostringstream ss_min, ss_max;
        ss_min << std::fixed << std::setprecision(4) << data_min;
        ss_max << std::fixed << std::setprecision(4) << data_max;
        cv::putText(img, ss_min.str(), cv::Point(cb_x, cb_y + cb_h + 12),
                    cv::FONT_HERSHEY_SIMPLEX, 0.35, cv::Scalar(0, 0, 0), 1);
        cv::putText(img, ss_max.str(), cv::Point(cb_x + cb_w - 50, cb_y + cb_h + 12),
                    cv::FONT_HERSHEY_SIMPLEX, 0.35, cv::Scalar(0, 0, 0), 1);
        if (!field_name.empty()) {
            cv::putText(img, field_name, cv::Point(cb_x + cb_w / 2 - 20, cb_y - 5),
                        cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(0, 0, 0), 1);
        }
    }
    
    // Frame number label
    cv::putText(img, "Frame " + std::to_string(frame_idx),
                cv::Point(width - 100, 20), cv::FONT_HERSHEY_SIMPLEX, 0.4,
                cv::Scalar(0, 0, 0), 1);
    
    return img;
}

void Visualization::generateVideo(const VisData& vis_data,
                                   const std::string& output_path,
                                   const std::string& field_name) {
    if (vis_data.n_frames == 0) {
        std::cerr << "Warning: No frames to generate video" << std::endl;
        return;
    }
    
    int width = 800;
    int height = 600;
    int fps = config_.video_fps;
    
    // Parse codec from config (e.g., "MJPG")
    int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    if (config_.video_codec.size() >= 4) {
        fourcc = cv::VideoWriter::fourcc(
            config_.video_codec[0], config_.video_codec[1],
            config_.video_codec[2], config_.video_codec[3]);
    }
    
    // Determine data range across all frames for consistent colormap
    double data_min = std::numeric_limits<double>::max();
    double data_max = std::numeric_limits<double>::lowest();
    
    std::string active_field = field_name;
    if (active_field.empty() && !vis_data.FaceScalars.empty()) {
        active_field = vis_data.FaceScalars.begin()->first;
    }
    
    if (!active_field.empty()) {
        auto it = vis_data.FaceScalars.find(active_field);
        if (it != vis_data.FaceScalars.end()) {
            for (const auto& frame_data : it->second) {
                for (double v : frame_data) {
                    if (std::isfinite(v)) {
                        data_min = std::min(data_min, v);
                        data_max = std::max(data_max, v);
                    }
                }
            }
        }
    }
    if (data_min >= data_max) { data_min = 0; data_max = 1; }
    
    // Override with manual range if configured
    if (config_.colormap_range_mode == "manual") {
        data_min = config_.colormap_min;
        data_max = config_.colormap_max;
    }
    
    std::cout << "Generating video: " << output_path 
              << " (" << vis_data.n_frames << " frames, " << fps << " fps, field=" 
              << active_field << ", range=[" << data_min << ", " << data_max << "])" << std::endl;
    
    // Create output directory if needed
    std::filesystem::create_directories(std::filesystem::path(output_path).parent_path());
    
    cv::VideoWriter writer(output_path, fourcc, fps, cv::Size(width, height));
    if (!writer.isOpened()) {
        std::cerr << "Error: Failed to open video writer: " << output_path << std::endl;
        return;
    }
    
    for (size_t i = 0; i < vis_data.n_frames; ++i) {
        cv::Mat frame = renderFrame(vis_data, static_cast<int>(i),
                                     active_field, data_min, data_max,
                                     width, height);
        writer.write(frame);
    }
    
    writer.release();
    std::cout << "✓ Video saved: " << output_path << std::endl;
}

} // namespace cppxdic
