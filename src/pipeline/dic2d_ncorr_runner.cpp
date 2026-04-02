#include "cppxdic/pipeline/dic2d_ncorr_runner.h"

#include "mat_writer.h"
#include "roi_manager.h"
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

namespace cppxdic::pipeline {

Dic2DNcorrRunner::Dic2DNcorrRunner(const Config& config, const BaseParameters& base_params)
    : config_(config),
      base_params_(base_params) {}

ncorr::DIC_analysis_output Dic2DNcorrRunner::run(const cv::Mat& ref_img,
                                                 const std::vector<cv::Mat>& cur_imgs,
                                                 const cv::Mat& roi_mask,
                                                 const SeedPoint& seed_point,
                                                 const StepParameters& step_params,
                                                 const std::string& output_path,
                                                 bool go_parallel,
                                                 bool use_no_update) const {
    std::cout << "Running ncorr DIC analysis..." << std::endl;
    std::cout << "  Radius: " << step_params.radius << ", Spacing: " << step_params.spacing << std::endl;
    std::cout << "  Scalefactor: " << (step_params.spacing + 1) << " (spacing + 1)" << std::endl;
    std::cout << "  Seed: (" << seed_point.pw[0] << ", " << seed_point.pw[1] << ")" << std::endl;

    std::vector<ncorr::Image2D> ncorr_images;
    std::vector<std::string> temp_image_paths;

    const std::string temp_dir = base_params_.outputPath + "/tmp_ncorr_images";
    std::filesystem::create_directories(temp_dir);

    const std::string ref_path = temp_dir + "/ref.png";
    cv::imwrite(ref_path, ref_img);
    ncorr_images.emplace_back(ref_path);
    temp_image_paths.push_back(ref_path);

    for (size_t i = 0; i < cur_imgs.size(); ++i) {
        std::ostringstream oss;
        oss << temp_dir << "/cur_" << std::setw(4) << std::setfill('0') << i << ".png";
        const std::string cur_path = oss.str();
        cv::imwrite(cur_path, cur_imgs[i]);
        ncorr_images.emplace_back(cur_path);
        temp_image_paths.push_back(cur_path);
    }

    ncorr::ROI2D roi = ROIManager::matToNcorrROI(roi_mask);
    cv::imwrite(temp_dir + "/roi_mask.png", get_cv_img(roi.get_mask(), 0, 255));

    const int scalefactor = step_params.spacing + 1;
    ncorr::DIC_analysis_input dic_input(
        ncorr_images,
        roi,
        scalefactor,
        ncorr::INTERP::QUINTIC_BSPLINE_PRECOMPUTE,
        ncorr::SUBREGION::CIRCLE,
        step_params.radius,
        step_params.total_threads,
        use_no_update ? ncorr::DIC_analysis_config::NO_UPDATE : ncorr::DIC_analysis_config::KEEP_MOST_POINTS,
        config_.debug_mode
    );

    ncorr::DIC_analysis_output dic_output_raw;
    if (go_parallel) {
        std::cout << "  Using parallel DIC processing..." << std::endl;
        std::vector<ncorr::SeedParams> seeds;
        seeds.emplace_back(seed_point.pw[0], seed_point.pw[1]);
        ncorr::DIC_analysis_parallel_input dic_parallel_input(dic_input, seeds);
        dic_output_raw = ncorr::matlab_DIC_analysis_parallel(dic_parallel_input);
    } else {
        std::cout << "  Using Matlab-style sequential DIC processing..." << std::endl;
        dic_output_raw = ncorr::matlab_DIC_analysis_sequential(
            dic_input,
            {ncorr::SeedParams(seed_point.pw[0], seed_point.pw[1])},
            false
        );
    }

    std::cout << "Post-processing displacements..." << std::endl;
    ncorr::DIC_analysis_output dic_eulerian_pixels = ncorr::change_perspective_with_inversion(
        dic_output_raw,
        ncorr::INTERP::CUBIC_KEYS
    );
    ncorr::DIC_analysis_output dic_lagrangian = ncorr::set_units(dic_output_raw, "mm", config_.units_per_pixel);
    ncorr::DIC_analysis_output dic_eulerian = ncorr::set_units(dic_eulerian_pixels, "mm", config_.units_per_pixel);
    (void)dic_lagrangian;
    std::cout << "  Created both Lagrangian and Eulerian perspectives" << std::endl;

    if (config_.debug_mode) {
        const std::string video_dir = std::filesystem::path(output_path).parent_path().string() + "/debug_video/";
        std::filesystem::create_directories(video_dir);
        const std::string base_name = std::filesystem::path(output_path).stem().string();
        const double alpha = config_.video_alpha;
        const double fps = static_cast<double>(config_.video_fps);

        std::cout << "  Saving debug DIC videos to " << video_dir << std::endl;
        try {
            ncorr::save_DIC_video(video_dir + base_name + "_v_eulerian.avi",
                                  dic_input, dic_eulerian, ncorr::DISP::V, alpha, fps);
            ncorr::save_DIC_video(video_dir + base_name + "_u_eulerian.avi",
                                  dic_input, dic_eulerian, ncorr::DISP::U, alpha, fps);
            std::cout << "  ✓ Debug DIC videos saved" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "  Warning: Failed to save debug videos: " << e.what() << std::endl;
        }
    }

    save(dic_output_raw, output_path);
    writeMatSidecar(output_path, ref_img, cur_imgs, roi_mask, step_params, dic_output_raw);
    std::cout << "DIC analysis saved: " << output_path << std::endl;

    return dic_output_raw;
}

void Dic2DNcorrRunner::writeMatSidecar(const std::string& output_path,
                                       const cv::Mat& ref_img,
                                       const std::vector<cv::Mat>& cur_imgs,
                                       const cv::Mat& roi_mask,
                                       const StepParameters& step_params,
                                       const ncorr::DIC_analysis_output& dic_output) const {
    if (cur_imgs.empty()) {
        return;
    }

    const std::filesystem::path mat_path = std::filesystem::path(output_path).replace_extension(".mat");
    std::vector<cv::Mat> cur_rois(cur_imgs.size(), roi_mask.clone());
    std::vector<ncorr::DIC_analysis_output> dic_outputs = {dic_output};

    std::map<std::string, double> dispinfo = {
        {"cutoff_corrcoef", config_.ncorr_cutoff_corrcoef},
        {"cutoff_diffnorm", step_params.cutoff_diffnorm},
        {"cutoff_iteration", static_cast<double>(step_params.cutoff_iteration)},
        {"lenscoef", 0.0},
        {"pixtounits", config_.units_per_pixel},
        {"radius", static_cast<double>(step_params.radius)},
        {"spacing", static_cast<double>(step_params.spacing)},
        {"subsettrunc", 0.0},
        {"total_threads", static_cast<double>(step_params.total_threads)}
    };

    if (!MatWriter::writeMultiFrameNcorrFile(mat_path.string(),
                                             ref_img,
                                             cur_imgs,
                                             roi_mask,
                                             cur_rois,
                                             dic_outputs,
                                             dispinfo)) {
        std::cerr << "  Warning: failed to write ncorr MAT sidecar: "
                  << mat_path << std::endl;
    }
}

} // namespace cppxdic::pipeline
