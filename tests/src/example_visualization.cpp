/**
 * Example: Using the Visualization Module
 * 
 * This example demonstrates how to load DIC results from binary cache
 * and export them for visualization in external tools (ParaView, MeshLab, etc.)
 * 
 * Equivalent to MATLAB's plot_singleTrial_map function
 */

#include "visualization.h"
#include "config.h"
#include <iostream>
#include <filesystem>

int main(int argc, char* argv[]) {
    std::cout << "==============================================\n";
    std::cout << "DIC Results Visualization Export Example\n";
    std::cout << "==============================================\n\n";
    
    try {
        // 1. Configure settings (equivalent to MATLAB's optStructTrial and optStructPlot)
        Config config;
        
        // Trial information (from MATLAB lines 150-159)
        config.base_path = "/Users/jaoga/devlab/MultiDIC";
        config.subject_id = "S09";
        config.phase_id = "loading";
        config.material = "glass";
        config.num_pair = 2;
        config.fileversion = "v2";
        
        // 3D Map settings (from MATLAB lines 161-167)
        config.mapLogic = true;
        config.plotopt = {"Epc1", "Epc2"};  // Principal strains
        config.deftype = "both";            // Both cumulative and rate
        config.viewplot = "below";
        config.contactAreaLogic = false;
        config.gapLogic = true;
        config.maxCorrCoeff = 10.0;
        config.format = "small";
        
        // Filtering settings (from MATLAB lines 169-176)
        config.smoothTimeLogic = true;
        config.filterFreq = 5.0;           // 5 Hz cutoff
        config.smoothSpaceLogic = true;
        config.smoothPar_n = 30;
        config.smoothPar_sigma = 2.0;
        
        // Additional plot settings (from MATLAB lines 178-181)
        config.showRobotLogic = false;     // No robot data in this example
        config.showImgLogic = false;
        config.camNbr = 0;
        
        // Export settings (C++ specific)
        config.export_format = "vtk";      // VTK for ParaView
        config.export_each_frame = false;  // Export all frames
        
        std::cout << "Configuration:\n";
        std::cout << "  Subject: " << config.subject_id << "\n";
        std::cout << "  Phase: " << config.phase_id << "\n";
        std::cout << "  Material: " << config.material << "\n";
        std::cout << "  File version: " << config.fileversion << "\n";
        std::cout << "  Plot fields: ";
        for (const auto& field : config.plotopt) {
            std::cout << field << " ";
        }
        std::cout << "\n";
        std::cout << "  Export format: " << config.export_format << "\n\n";
        
        // 2. Create visualization manager
        cppxdic::Visualization viz(config);
        
        // 3. Load DIC results from binary cache
        // Path matches MATLAB: baseDICPathFaster/subject/material/trial/phase/DIC3DPPresults_NPairs_cum_version.bin
        std::string trial_num = "007";
        std::string cache_path = config.base_path + "/analysis/" + 
                                 config.subject_id + "/" + 
                                 config.material + "/" + 
                                 trial_num + "/" + 
                                 config.phase_id + "/";
        
        std::string cache_file = cache_path + 
                                "DIC3DPPresults_" + std::to_string(config.num_pair) + 
                                "Pairs_cum_" + config.fileversion + ".bin";
        
        std::cout << "Loading from: " << cache_file << "\n\n";
        
        if (!std::filesystem::exists(cache_file)) {
            std::cerr << "ERROR: Cache file not found!\n";
            std::cerr << "Make sure Step F (deformation) has been completed.\n";
            return 1;
        }
        
        auto results = viz.loadFromBinaryCache(cache_file);
        
        // 4. Print trial information (equivalent to MATLAB lines 111-123)
        viz.printTrialInfo(results);
        
        // 5. Apply temporal filtering (equivalent to MATLAB lines 167-169)
        std::cout << "\nApplying filters...\n";
        viz.applyTemporalFilter(results);
        
        // 6. Prepare visualization data
        std::cout << "\nPreparing visualization data...\n";
        auto vis_data = viz.prepareVisualizationData(results);
        
        // 7. Export data for visualization
        std::string output_dir = cache_path + "viz";
        std::filesystem::create_directories(output_dir);
        
        std::string output_base = output_dir + "/" + 
                                 config.subject_id + "-" + 
                                 config.phase_id + "-" + 
                                 trial_num;
        
        std::cout << "\nExporting visualization data...\n";
        viz.exportData(vis_data, output_base);
        
        // 8. Generate summary statistics
        std::string stats_file = output_base + "_summary.txt";
        viz.generateSummaryStats(results, stats_file);
        
        std::cout << "\n==============================================\n";
        std::cout << "SUCCESS!\n";
        std::cout << "==============================================\n";
        std::cout << "Output files:\n";
        std::cout << "  - Visualization: " << output_base << "." << config.export_format << "\n";
        std::cout << "  - Statistics: " << stats_file << "\n\n";
        std::cout << "To view results:\n";
        if (config.export_format == "vtk") {
            std::cout << "  1. Open ParaView\n";
            std::cout << "  2. Load: " << output_base << ".vtk\n";
            std::cout << "  3. Select 'Surface' representation\n";
            std::cout << "  4. Choose field from dropdown (Epc1, Epc2, etc.)\n";
        } else if (config.export_format == "ply") {
            std::cout << "  1. Open MeshLab\n";
            std::cout << "  2. Load: " << output_base << ".ply\n";
        } else if (config.export_format == "csv") {
            std::cout << "  1. Open with Excel or Python\n";
            std::cout << "  2. File: " << output_base << ".csv\n";
        }
        std::cout << "\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\nERROR: " << e.what() << "\n";
        return 1;
    }
}
