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
#include <memory>

namespace cppxdic {

// DLT calibration data (loaded from DLTstruct_cam_<id>.mat)
// Matches MATLAB DLTstructCam struct fields
struct DLTCalibrationData {
    std::vector<double> DLTparams;          // 11 DLT parameters
    std::vector<double> C3Dtrue;            // 3D calibration points (dim0 x dim1 x 3, column-major flattened to row-major)
    size_t C3Dtrue_dim0 = 0;               // First dimension (e.g., 10)
    size_t C3Dtrue_dim1 = 0;               // Second dimension (e.g., 45)
    std::vector<double> imageCentroids;     // 2D image centroids (Nx2, row-major)
    size_t imageCentroids_rows = 0;         // Number of centroid points
    std::vector<uint8_t> columns;           // Column indices used in calibration
    int indCam = 0;                         // Camera index
    std::string filePath;                   // Source file path (for traceability)
};

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
    // -------- RAII helpers (reusable across the project) --------
    struct MatFileDeleter {
        void operator()(mat_t* f) const noexcept { if (f) Mat_Close(f); }
    };
    struct MatVarDeleter {
        void operator()(matvar_t* v) const noexcept { if (v) Mat_VarFree(v); }
    };

    using MatFilePtr = std::unique_ptr<mat_t, MatFileDeleter>;
    using MatVarPtr  = std::unique_ptr<matvar_t, MatVarDeleter>;

    // Optional convenience helpers (non-breaking additions)
    static MatFilePtr openMat(const std::string& path);
    static MatVarPtr  readVar(mat_t* matfp, const std::string& name);     // top-level
    static MatVarPtr  readFirstVar(mat_t* matfp);                         // first variable in file

    // DLT calibration loading (DLTstruct_cam_<id>.mat)
    static bool loadDLTCalibration(const std::string& mat_path, DLTCalibrationData& calib);

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
     * @return Vector of doubles
     */
    static std::vector<double> readDouble2DArray(matvar_t* var, size_t& rows, size_t& cols);
    
    /**
     * Read image from matvar
     * 
     * @param var MAT variable
     * @return OpenCV Mat
     */
    static cv::Mat readImage(matvar_t* var);
    
    /**
     * Read scalar from matvar
     * 
     * @param var MAT variable
     * @return Scalar value
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
     * Get cell array size
     * 
     * @param var MAT variable
     * @return Number of elements
     */
    static size_t getCellArraySize(matvar_t* var);

    /**
     * Get cell from cell array
     * 
     * @param var MAT variable
     * @param index Index of cell to get
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
     * @param mat_path Path to MAT file
     * @param combined Output structure
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
     * Read int array from matvar
     * 
     * @param var MAT variable
     * @return Vector of ints
     */
    static std::vector<int> readIntArray(matvar_t* var);

private:
    // Internal helpers to reduce repetition across all readers
    static matvar_t* getStructField(matvar_t* s, const char* field, size_t index = 0) noexcept;

    static bool readTopInt(mat_t* matfp, const char* name, int& out) noexcept;
    static bool readTopDouble(mat_t* matfp, const char* name, double& out) noexcept;
    static bool readTopString(mat_t* matfp, const char* name, std::string& out);

    static bool readStructInt(matvar_t* s, const char* field, int& out, size_t index = 0) noexcept;
    static bool readStructDouble(matvar_t* s, const char* field, double& out, size_t index = 0) noexcept;
    static bool readStructBool(matvar_t* s, const char* field, bool& out, size_t index = 0) noexcept;
    static bool readStructString(matvar_t* s, const char* field, std::string& out, size_t index = 0);
};

} // namespace cppxdic

#endif // MAT_READER_H
