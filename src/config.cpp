/**
 * Configuration implementation for CPPXDIC
 */

#include "config.h"
#include <iostream>
#include <filesystem>

void Config::loadGlobalParams() {
    // Set default paths based on current working directory
    setDefaultPaths();
    
    // Initialize derived paths
    data_path = base_path + "/example_data";
    dic_path = base_path + "/analysis";
    
    std::cout << "Global parameters loaded." << std::endl;
}

void Config::loadDicParams() {
    // All parameters are already initialized with default values
    // In a real implementation, these could be loaded from a config file
    
    std::cout << "DIC parameters loaded." << std::endl;
}

void Config::updateVariables() {
    // Update material name from material_id
    if (material_id >= 1 && material_id <= static_cast<int>(frictional_conditions.size())) {
        material = frictional_conditions[material_id - 1];  // Convert to 0-based index
    } else {
        material = "unknown";
        std::cerr << "Warning: Invalid material_id " << material_id << std::endl;
    }
    
    std::cout << "Variables updated. Material: " << material << std::endl;
}

void Config::setDefaultPaths() {
    // Set base path to current working directory or a reasonable default
    if (std::filesystem::exists(base_path)) return;
    try {
        base_path = std::filesystem::current_path().string();
    } catch (const std::exception& e) {
        base_path = ".";
        std::cerr << "Warning: Could not get current path, using '.' as base_path" << std::endl;
    }
}
