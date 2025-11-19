/**
 * Visualization module for DIC results
 * Exports 3D mesh data with deformation/strain fields for visualization
 * Equivalent to MATLAB's plot_singleTrial_map and anim8_rewrited functions
 */

#ifndef VISUALIZATION_H
#define VISUALIZATION_H

#include "dic_structures.h"
#include "config.h"
#include <string>
#include <vector>
#include <map>

namespace cppxdic {

/**
 * Visualization export formats
 */
enum class ExportFormat {
    VTK,    // VTK Legacy format (for ParaView, VisIt)
    PLY,    // PLY format (for MeshLab, CloudCompare)
    CSV     // CSV format (for custom analysis)
};

/**
 * Visualization data container
 * Holds filtered and processed data ready for export
 */
struct VisData {
    std::vector<Points3D> Points3D;              // 3D points per frame
    std::vector<int> Faces;                       // Triangle faces (Nx3 flattened)
    std::vector<std::vector<double>> FaceColors; // Face colors per frame
    std::map<std::string, std::vector<std::vector<double>>> FaceScalars;  // Scalar fields per frame
    std::map<std::string, std::vector<std::vector<double>>> FaceVectors;  // Vector fields per frame
    size_t n_frames;
};

/**
 * Visualization manager class
 * Handles loading, filtering, and exporting DIC results
 */
class Visualization {
public:
    Visualization(const Config& config);
    ~Visualization() = default;

    /**
     * Load DIC results from binary cache file
     * @param filepath Path to the binary cache file (.bin)
     * @return DIC3DPPresults structure
     */
    DIC3DPPresults loadFromBinaryCache(const std::string& filepath);

    /**
     * Apply temporal filtering to measures
     * Equivalent to MATLAB's filter_measures function
     * @param results DIC results to filter (modified in place)
     */
    void applyTemporalFilter(DIC3DPPresults& results);

    /**
     * Apply spatial smoothing to face measures
     * @param face_data Face scalar data to smooth
     * @param faces Triangle connectivity
     * @return Smoothed face data
     */
    std::vector<double> applySpatialSmooth(
        const std::vector<double>& face_data,
        const std::vector<int>& faces);

    /**
     * Prepare visualization data from DIC results
     * Extracts requested fields (Epc1, Epc2, DispX, etc.) and applies filtering
     * @param results DIC results
     * @return Visualization data structure
     */
    VisData prepareVisualizationData(const DIC3DPPresults& results);

    /**
     * Export visualization data to file
     * @param vis_data Visualization data
     * @param output_path Output file path (without extension)
     * @param frame_idx Frame index (-1 for all frames)
     */
    void exportData(const VisData& vis_data, 
                    const std::string& output_path, 
                    int frame_idx = -1);

    /**
     * Export single frame to VTK format
     */
    void exportFrameVTK(const VisData& vis_data, 
                        const std::string& filepath, 
                        int frame_idx);

    /**
     * Export single frame to PLY format
     */
    void exportFramePLY(const VisData& vis_data, 
                        const std::string& filepath, 
                        int frame_idx);

    /**
     * Export single frame to CSV format
     */
    void exportFrameCSV(const VisData& vis_data, 
                        const std::string& filepath, 
                        int frame_idx);

    /**
     * Generate summary statistics file
     * @param results DIC results
     * @param output_path Output file path
     */
    void generateSummaryStats(const DIC3DPPresults& results, 
                              const std::string& output_path);

    /**
     * Print trial information (equivalent to MATLAB's fprintf info)
     * @param results DIC results
     */
    void printTrialInfo(const DIC3DPPresults& results);

private:
    Config config_;
    ExportFormat export_format_;

    /**
     * Convert export format string to enum
     */
    ExportFormat parseExportFormat(const std::string& format_str);

    /**
     * Extract scalar field from DIC results
     * @param results DIC results
     * @param field_name Field name (e.g., "Epc1", "Epc2", "J", "DispX")
     * @return Vector of scalar values per frame
     */
    std::vector<std::vector<double>> extractScalarField(
        const DIC3DPPresults& results, 
        const std::string& field_name);

    /**
     * Apply correlation coefficient filtering
     * Sets values to NaN where correlation exceeds threshold
     */
    void applyCorrelationFilter(std::vector<std::vector<double>>& face_data,
                                 const std::vector<std::vector<double>>& corr_data);

    /**
     * Apply gap filtering (remove stitched boundaries)
     */
    void applyGapFilter(std::vector<std::vector<double>>& face_data,
                        const std::vector<int>& FacePairInds);

    /**
     * Compute principal strains from strain tensor
     * Returns Epc1 and Epc2 (principal strains)
     */
    void computePrincipalStrains(const DIC3DPPresults& results,
                                  std::vector<std::vector<double>>& Epc1,
                                  std::vector<std::vector<double>>& Epc2);

    /**
     * Apply low-pass Butterworth filter to time series
     * @param data Time series data (rows = time, cols = spatial points)
     * @param cutoff_freq Cutoff frequency (Hz)
     * @param sample_freq Sampling frequency (Hz)
     * @return Filtered data
     */
    std::vector<std::vector<double>> butterworthFilter(
        const std::vector<std::vector<double>>& data,
        double cutoff_freq,
        double sample_freq);
};

} // namespace cppxdic

#endif // VISUALIZATION_H
