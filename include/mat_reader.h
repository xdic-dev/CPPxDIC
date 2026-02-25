/**
 * MAT File Reader for CPPXDIC
 * Helper functions for reading MATLAB .mat files using matio
 */

#ifndef MAT_READER_H
#define MAT_READER_H

#include <matio.h>
#include <opencv2/core.hpp>
#include <string>
#include <vector>
#include <map>

namespace cppxdic {

// Protocol file data structure (loaded from MAT file)
struct ProtocolFileData {
    struct TrialEntry {
        int trial_number;
        std::string direction;  // "Ubnf", "Rbnf", etc.
        double force;          // Normal force in N
        double speed;          // Speed in mm/s
        int repetition;        // Repetition number
    };
    
    std::vector<std::string> titles;  // Column names
    std::vector<TrialEntry> trials;   // Trial information
    size_t n_trials;
    size_t n_fields;
};

/**
 * MatReader class
 * Provides utilities for reading MAT files
 */
class MatReader {
public:
    // Protocol loading
    static bool loadProtocol(const std::string& protocol_path, ProtocolFileData& protocol);
    static std::vector<std::string> readCellStrings(matvar_t* cell_var);
    /**
     * Read a specific field from a struct variable
     * 
     * @param matfp MAT file pointer
     * @param struct_name Name of the struct variable
     * @param field_name Name of the field to read
     * @return Field variable pointer (caller must free with Mat_VarFree)
     */
    static matvar_t* readStructField(mat_t* matfp, 
                                     const std::string& struct_name,
                                     const std::string& field_name);
    
    /**
     * Read a nested struct field (e.g., "data_dic_save.dispinfo.radius")
     * 
     * @param matfp MAT file pointer
     * @param path Dot-separated path to field
     * @return Field variable pointer (caller must free)
     */
    static matvar_t* readNestedField(mat_t* matfp, const std::string& path);
    
    /**
     * Read double array from matvar
     * 
     * @param var MAT variable
     * @return Vector of doubles
     */
    static std::vector<double> readDoubleArray(matvar_t* var);
    
    /**
     * Read 2D double array from matvar
     * 
     * @param var MAT variable
     * @param rows Output: number of rows
     * @param cols Output: number of columns
     * @return Flattened vector (row-major)
     */
    static std::vector<double> readDouble2DArray(matvar_t* var, size_t& rows, size_t& cols);
    
    /**
     * Read cv::Mat (image) from matvar
     * 
     * @param var MAT variable
     * @return OpenCV Mat
     */
    static cv::Mat readImage(matvar_t* var);
    
    /**
     * Read scalar double value
     * 
     * @param var MAT variable
     * @return Double value
     */
    static double readScalar(matvar_t* var);
    
    /**
     * Read string from matvar
     * 
     * @param var MAT variable
     * @return String value
     */
    static std::string readString(matvar_t* var);
    
    /**
     * Read cell array size
     * 
     * @param var MAT cell variable
     * @return Number of cells
     */
    static size_t getCellArraySize(matvar_t* var);
    
    /**
     * Get cell from cell array
     * 
     * @param var MAT cell variable
     * @param index Cell index
     * @return Cell variable (do not free - part of parent)
     */
    static matvar_t* getCellElement(matvar_t* var, size_t index);
    
    /**
     * Read dispinfo parameters from MAT file
     * 
     * @param matfp MAT file pointer
     * @return Map of parameter names to values
     */
    static std::map<std::string, double> readDispInfo(mat_t* matfp);
    
    /**
     * Check if variable exists in MAT file
     * 
     * @param matfp MAT file pointer
     * @param var_name Variable name
     * @return True if exists
     */
    static bool variableExists(mat_t* matfp, const std::string& var_name);
    
    /**
     * Read ROI mask from MAT file
     * 
     * @param mat_path Path to MAT file
     * @param var_name Variable name (default: "refmask")
     * @return ROI mask as cv::Mat
     */
    static cv::Mat readROIMask(const std::string& mat_path, 
                               const std::string& var_name = "refmask");
    
    /**
     * Read DIC3Dcombined structure from MAT file
     * 
     * @param mat_path Path to MAT file containing DIC3Dcombined
     * @param combined Output: DIC3Dcombined structure
     * @return True if successful
     */
    static bool readDIC3Dcombined(const std::string& mat_path, 
                                  struct DIC3Dcombined& combined);
    
    /**
     * Read DIC2DPairResults structure from MAT file
     * Loads myDIC2DpairResults_C_X_C_Y.mat files
     * 
     * @param mat_path Path to MAT file containing DIC2DpairResults
     * @param result Output: DIC2DPairResults structure
     * @return True if successful
     */
    static bool readDIC2DPairResults(const std::string& mat_path,
                                     struct DIC2DPairResults& result);
    
    /**
     * Read integer array from matvar
     * 
     * @param var MAT variable
     * @return Vector of integers
     */
    static std::vector<int> readIntArray(matvar_t* var);
};

} // namespace cppxdic

#endif // MAT_READER_H
