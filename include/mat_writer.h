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
#include <Eigen/Dense>
#include <string>
#include <vector>
#include <map>
#include "dic_structures.h"
#include "strain_computation.h"

namespace cppxdic::io::mat {
class MatNcorrWriter;
}

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
     * Write multi-frame ncorr files (ncorr1.mat, ncorr2.mat, etc.)
     * For tracking DIC with multiple current frames
     * 
     * @param filename Output MAT filename
     * @param ref_img Reference image
     * @param cur_imgs Vector of current images (one per frame)
     * @param ref_roi Reference ROI
     * @param cur_rois Vector of current ROIs (one per frame)
     * @param dic_outputs Vector of DIC outputs (one per frame)
     * @param dispinfo DIC parameters
     * @param type_str Type string for all frames (default: "load")
     * @param ref_name Reference image name (default: "reference")
     * @return Success status
     */
    static bool writeMultiFrameNcorrFile(const std::string& filename,
                                        const cv::Mat& ref_img,
                                        const std::vector<cv::Mat>& cur_imgs,
                                        const cv::Mat& ref_roi,
                                        const std::vector<cv::Mat>& cur_rois,
                                        const std::vector<ncorr::DIC_analysis_output>& dic_outputs,
                                        const std::map<std::string, double>& dispinfo,
                                        const std::string& type_str = "load",
                                        const std::string& ref_name = "reference");
    
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
                                       const DIC3Dcombined& combined,
                                       const std::string& struct_name = "DIC3Dcombined");
    
    /**
     * Build combined struct fields as a matvar_t* (for embedding in parent struct)
     * Does NOT write to file. Caller is responsible for writing and freeing.
     * 
     * @param matfp MAT file pointer (needed for AllPairsResults/DIC2Dinfo)
     * @param combined 3D combined results structure
     * @param struct_name Name of the wrapping struct
     * @param extra_fields Additional field names to add to the struct
     * @return matvar_t* struct with all fields populated (caller must write+free)
     */
    static matvar_t* buildCombinedStructFields(mat_t* matfp,
                                                const DIC3Dcombined& combined,
                                                const std::string& struct_name,
                                                const std::vector<std::string>& extra_fields = {});
    
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
    
    /**
     * Write deformation fields to HDF5 group
     * Writes all 36 deformation measures as cell arrays
     * 
     * @param matfp MAT file pointer (must be open for writing)
     * @param group_name Name of the group (e.g., "Deform")
     * @param deform_data Frame-based deformation results
     * @return Success status
     */
    static bool writeDeformationGroup(mat_t* matfp,
                                      const std::string& group_name,
                                      const FrameDeformationResult& deform_data);
    
    /**
     * Build deformation struct as matvar_t* for embedding in parent struct
     * Does NOT write to file. Caller owns the returned pointer.
     */
    static matvar_t* buildDeformationStruct(const std::string& group_name,
                                             const FrameDeformationResult& deform_data);
    
    /**
     * Write displacement group (DispVec, DispMgn)
     * 
     * @param matfp MAT file pointer (must be open for writing)
     * @param disp_vec Displacement vectors per frame (Nx3 per frame)
     * @param disp_mgn Displacement magnitudes per frame (Nx1 per frame)
     * @param n_frames Number of frames
     * @return Success status
     */
    static bool writeDisplacementGroup(mat_t* matfp,
                                       const std::vector<std::vector<Eigen::Vector3d>>& disp_vec,
                                       const std::vector<std::vector<double>>& disp_mgn,
                                       size_t n_frames);
    
    /**
     * Write face-based cell arrays (FaceCentroids, FaceCorrComb, FaceIsoInd)
     * 
     * @param matfp MAT file pointer (must be open for writing)
     * @param face_centroids Face centroids per frame (Mx3 per frame)
     * @param face_corr_comb Face correlation coefficients per frame (Mx1 per frame)
     * @param face_iso_ind Face isotropy indices per frame (Mx1 per frame)
     * @param n_frames Number of frames
     * @return Success status
     */
    static bool writeFaceArrays(mat_t* matfp,
                               const std::vector<std::vector<Eigen::Vector3d>>& face_centroids,
                               const std::vector<std::vector<double>>& face_corr_comb,
                               const std::vector<std::vector<double>>& face_iso_ind,
                               size_t n_frames);
    
    /**
     * Write calibration group (DLTparameters, DLTpath)
     * 
     * @param matfp MAT file pointer (must be open for writing)
     * @param calibration Calibration data structure
     * @return Success status
     */
    static bool writeCalibrationGroup(mat_t* matfp,
                                      const CalibrationData& calibration);
    
    /**
     * Write distortion group (distortionModel, distortionPath)
     * 
     * @param matfp MAT file pointer (must be open for writing)
     * @param distortion Distortion data structure
     * @return Success status
     */
    static bool writeDistortionGroup(mat_t* matfp,
                                    const DistortionData& distortion);
    
    /**
     * Write AllPairsResults cell array with full DIC3DpairResults structs
     * @param matfp MAT file pointer
     * @param all_pairs Vector of DIC3DpairResults (individual pair results)
     * @return Success status
     */
    static bool writeAllPairsResults(mat_t* matfp,
                                    const std::vector<DIC3DpairResults>& all_pairs);
    
    /**
     * Write DIC2Dinfo as object array of DIC2DPairResults
     * 
     * @param matfp MAT file pointer (must be open for writing)
     * @param dic2d_info Vector of 2D DIC results
     * @return Success status
     */
    static bool writeDIC2Dinfo(mat_t* matfp,
                              const std::vector<DIC2DPairResults>& dic2d_info);

private:
    friend class io::mat::MatNcorrWriter;

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
    
    /**
     * Create cell array from scalar data (per frame)
     * 
     * @param name Variable name
     * @param data Vector of vectors (frame x elements)
     * @param n_frames Number of frames
     * @return Cell array variable
     */
    static matvar_t* createCellArrayFromScalars(
        const std::string& name,
        const std::vector<std::vector<double>>& data,
        size_t n_frames);
    
    /**
     * Create cell array from 3D vectors (per frame)
     * 
     * @param name Variable name
     * @param data Vector of vectors of Vector3d (frame x elements)
     * @param n_frames Number of frames
     * @return Cell array variable
     */
    static matvar_t* createCellArrayFromVectors(
        const std::string& name,
        const std::vector<std::vector<Eigen::Vector3d>>& data,
        size_t n_frames);
    
    /**
     * Create cell array from 3x3 matrices (per frame)
     * 
     * @param name Variable name
     * @param data Vector of vectors of Matrix3d (frame x elements)
     * @param n_frames Number of frames
     * @return Cell array variable
     */
    static matvar_t* createCellArrayFromMatrices(
        const std::string& name,
        const std::vector<std::vector<Eigen::Matrix3d>>& data,
        size_t n_frames);
    
    /**
     * Create 2D cell array from strings
     * 
     * @param name Variable name
     * @param data 2D vector of strings (rows x cols)
     * @param rows Number of rows
     * @param cols Number of columns
     * @return Cell array variable
     */
    static matvar_t* createCellArray2DFromStrings(
        const std::string& name,
        const std::vector<std::vector<std::string>>& data,
        size_t rows,
        size_t cols);
    
    /**
     * Create 2D cell array from double vectors
     * 
     * @param name Variable name
     * @param data 2D vector of double vectors (rows x cols, each element is a vector)
     * @param rows Number of rows
     * @param cols Number of columns
     * @return Cell array variable
     */
    static matvar_t* createCellArray2DFromVectors(
        const std::string& name,
        const std::vector<std::vector<std::vector<double>>>& data,
        size_t rows,
        size_t cols);
};

} // namespace cppxdic

#endif // MAT_WRITER_H
