/**
 * MAT File Writer for CPPXDIC
 * Handles writing DIC results to MATLAB v7.3 (HDF5) format
 * Produces output structure matching xDIC format
 */

#ifndef MAT_WRITER_H
#define MAT_WRITER_H

#include <matio.h>
#include <ncorr.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <map>
#include "dic_structures.h"

namespace cppxdic {

/**
 * MatWriter class
 * Provides utilities for writing MAT files with proper xDIC structure
 */
class MatWriter {
public:
    /**
     * Write MATCHING2xxx_pair.mat file
     * Structure:
     * - current_save (group)
     *   - gs, name, path, roi, type
     * - data_dic_save (group)
     *   - dispinfo, displacements, straininfo, strains
     * - reference_save (group)
     *   - gs, name, path, roi, type
     * 
     * @param filename Output MAT filename
     * @param ref_img Reference image
     * @param cur_img Current image
     * @param ref_roi Reference ROI
     * @param cur_roi Current ROI
     * @param dic_output DIC analysis output
     * @param dispinfo DIC parameters
     * @return Success status
     */
    static bool writeMatchingFile(const std::string& filename,
                                 const cv::Mat& ref_img,
                                 const cv::Mat& cur_img,
                                 const cv::Mat& ref_roi,
                                 const cv::Mat& cur_roi,
                                 const ncorr::DIC_analysis_output& dic_output,
                                 const std::map<std::string, double>& dispinfo);
    
    /**
     * Write MATCHING2xxx_pair.mat file with both Lagrangian and Eulerian perspectives
     * 
     * @param filename Output MAT filename
     * @param ref_img Reference image
     * @param cur_img Current image
     * @param ref_roi Reference ROI
     * @param cur_roi Current ROI
     * @param dic_lagrangian Lagrangian (reference) DIC output
     * @param dic_eulerian Eulerian (current) DIC output
     * @param dispinfo DIC parameters
     * @return Success status
     */
    static bool writeMatchingFile(const std::string& filename,
                                 const cv::Mat& ref_img,
                                 const cv::Mat& cur_img,
                                 const cv::Mat& ref_roi,
                                 const cv::Mat& cur_roi,
                                 const ncorr::DIC_analysis_output& dic_lagrangian,
                                 const ncorr::DIC_analysis_output& dic_eulerian,
                                 const std::map<std::string, double>& dispinfo);
    
    /**
     * Write REF_MASK_xxx_phase_pairX.mat file
     * Simple structure: refmask (uint8 2D array)
     * 
     * @param filename Output MAT filename
     * @param mask ROI mask
     * @return Success status
     */
    static bool writeROIMaskFile(const std::string& filename,
                                const cv::Mat& mask);
    
    /**
     * Write REF_SEED_xxx_phase_pairX.mat file
     * Simple structure: seed_point (uint16 1D array [x, y])
     * 
     * @param filename Output MAT filename
     * @param seed_x X coordinate
     * @param seed_y Y coordinate
     * @return Success status
     */
    static bool writeSeedFile(const std::string& filename,
                            int seed_x,
                            int seed_y);
    
    /**
     * Write dic_info_data_target_pairX.mat file
     * Structure:
     * - actual_fps_meas (int)
     * - idxframe (uint8 1D array)
     * 
     * @param filename Output MAT filename
     * @param fps FPS value
     * @param frame_indices Frame indices
     * @return Success status
     */
    static bool writeTrialInfoFile(const std::string& filename,
                                  double fps,
                                  const std::vector<int>& frame_indices);
    
    /**
     * Write ncorr1.mat / ncorr2.mat / ncorr12.mat files
     * Full DIC results with dispinfo and displacements
     * Structure matches xDIC output format with:
     * - input (DIC parameters)
     * - output (displacement fields, correlation coefficients)
     * 
     * @param filename Output MAT filename
     * @param dic_input DIC analysis input parameters
     * @param dic_output DIC analysis output
     * @return Success status
     */
    static bool writeDICResultFile(const std::string& filename,
                                  const ncorr::DIC_analysis_input& dic_input,
                                  const ncorr::DIC_analysis_output& dic_output);
    
    /**
     * Convert ncorr .bin output to .mat format
     * Wrapper to load .bin and save as MATLAB-compatible .mat
     * 
     * @param bin_path Path to .bin file
     * @param mat_path Output .mat file path
     * @param dic_input DIC input parameters
     * @return Success status
     */
    static bool convertBinToMat(const std::string& bin_path,
                                const std::string& mat_path,
                                const ncorr::DIC_analysis_input& dic_input);
    
    /**
     * Write myDIC2DpairResults_C_X_C_Y.mat file
     * Contains processed 2D DIC results with triangulation
     * 
     * @param filename Output MAT filename
     * @param results DIC 2D pair results structure
     * @return Success status
     */
    static bool writeDIC2DPairResults(const std::string& filename,
                                      const DIC2DPairResults& results);
    
    /**
     * Write DIC3Dcombined_XPairs_stitched.mat file
     * Contains 3D reconstruction results
     * 
     * @param filename Output MAT filename
     * @param combined 3D combined results structure
     * @return Success status
     */
    static bool write3DCombinedResults(const std::string& filename,
                                       const DIC3Dcombined& combined);
    
    /**
     * Write DIC3DPPresults_XPairs_Y_v1.mat file
     * Contains deformation and strain analysis results
     * 
     * @param filename Output MAT filename
     * @param ppresults Post-processing results structure
     * @return Success status
     */
    static bool write3DPPresults(const std::string& filename,
                                const DIC3DPPresults& ppresults);

private:
    /**
     * Create MAT file with HDF5 format (v7.3)
     * 
     * @param filename Output filename
     * @return MAT file pointer
     */
    static mat_t* createMatFileHDF5(const std::string& filename);
    
    /**
     * Create MAT file with standard format (v5)
     * 
     * @param filename Output filename
     * @return MAT file pointer
     */
    static mat_t* createMatFileV5(const std::string& filename);
    
    /**
     * Write cv::Mat as variable to MAT file
     * 
     * @param matfp MAT file pointer
     * @param varname Variable name
     * @param mat OpenCV Mat to write
     * @return Success status
     */
    static bool writeMatVariable(mat_t* matfp,
                                const std::string& varname,
                                const cv::Mat& mat);
    
    /**
     * Write array as variable to MAT file
     * 
     * @param matfp MAT file pointer
     * @param varname Variable name
     * @param data Data array
     * @param dims Dimensions
     * @param data_type MAT data type
     * @return Success status
     */
    static bool writeArrayVariable(mat_t* matfp,
                                  const std::string& varname,
                                  const void* data,
                                  const std::vector<size_t>& dims,
                                  matio_types data_type,
                                  matio_classes class_type);
    
    /**
     * Write string as variable to MAT file
     * 
     * @param matfp MAT file pointer
     * @param varname Variable name
     * @param str String value
     * @return Success status
     */
    static bool writeStringVariable(mat_t* matfp,
                                   const std::string& varname,
                                   const std::string& str);
    
    /**
     * Write scalar as variable to MAT file
     * 
     * @param matfp MAT file pointer
     * @param varname Variable name
     * @param value Scalar value
     * @return Success status
     */
    static bool writeScalarVariable(mat_t* matfp,
                                   const std::string& varname,
                                   double value);
    
    /**
     * Create struct variable in MAT file
     * 
     * @param struct_name Struct name
     * @param field_names Field names
     * @return Struct variable pointer
     */
    static matvar_t* createStructVariable(const std::string& struct_name,
                                         const std::vector<std::string>& field_names);
    
    /**
     * Add field to struct variable
     * 
     * @param struct_var Struct variable
     * @param field_name Field name
     * @param field_var Field variable
     * @param index Struct index (for struct arrays)
     * @return Success status
     */
    static bool addFieldToStruct(matvar_t* struct_var,
                                const std::string& field_name,
                                matvar_t* field_var,
                                size_t index = 0);
    
    /**
     * Convert ncorr Disp2D to displacement arrays
     * 
     * @param disp Displacement field
     * @param u_array Output: U displacement array
     * @param v_array Output: V displacement array
     * @param roi_mask Output: ROI mask
     */
    static void convertDispToArrays(const ncorr::Disp2D& disp,
                                   std::vector<double>& u_array,
                                   std::vector<double>& v_array,
                                   cv::Mat& roi_mask);
    
    /**
     * Format displacement info structure
     * Creates dispinfo struct with cutoff, radius, spacing, etc.
     * 
     * @param params DIC parameters
     * @return dispinfo struct variable
     */
    static matvar_t* formatDispInfo(const std::map<std::string, double>& params);
    
    /**
     * Format displacements structure
     * Creates displacements struct with plot fields
     * 
     * @param dic_output DIC output
     * @return displacements struct variable
     */
    static matvar_t* formatDisplacements(const ncorr::DIC_analysis_output& dic_output);
    
    /**
     * Format displacements structure with both Lagrangian and Eulerian perspectives
     * Creates displacements struct with _ref_formatted and _cur_formatted fields
     * 
     * @param dic_lagrangian Lagrangian (reference) DIC output
     * @param dic_eulerian Eulerian (current) DIC output
     * @return displacements struct variable
     */
    static matvar_t* formatDisplacements(const ncorr::DIC_analysis_output& dic_lagrangian,
                                        const ncorr::DIC_analysis_output& dic_eulerian);
    
    /**
     * Convert cv::Mat ROI mask to ncorr::ROI2D
     * 
     * @param mask OpenCV binary mask (CV_8U)
     * @return ROI2D object
     */
    static ncorr::ROI2D convertMatToROI2D(const cv::Mat& mask);
    
    /**
     * Convert ncorr::ROI2D to cv::Mat ROI mask
     * 
     * @param roi ROI2D object
     * @return OpenCV binary mask (CV_8U)
     */
    static cv::Mat convertROI2DToMat(const ncorr::ROI2D& roi);
    
    /**
     * Write DIC Ncorr file (common logic for MATCHING files)
     * Creates reference_save, current_save, and data_dic_save structures
     * 
     * @param filename Output MAT filename
     * @param ref_img Reference image
     * @param cur_img Current image
     * @param ref_roi Reference ROI
     * @param cur_roi Current ROI (will be updated with displacement if available)
     * @param dispinfo_var Formatted dispinfo struct variable
     * @param displacements_var Formatted displacements struct variable
     * @param dic_output DIC output (used for ROI update)
     * @return Success status
     */
    static bool writeDicNcorrFile(const std::string& filename,
                                 const cv::Mat& ref_img,
                                 const cv::Mat& cur_img,
                                 const cv::Mat& ref_roi,
                                 const cv::Mat& cur_roi,
                                 matvar_t* dispinfo_var,
                                 matvar_t* displacements_var,
                                 const ncorr::DIC_analysis_output& dic_output);
};

} // namespace cppxdic

#endif // MAT_WRITER_H
