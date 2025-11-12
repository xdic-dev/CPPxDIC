/**
 * DIC Analysis implementation for CPPXDIC
 */

#include "dic_analysis.h"
#include "utils.h"
#include "step_d_workflow.h"
#include <iostream>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <matio.h>
#include <limits>
#include <cctype>
#include <cmath>
#include <fstream>
// JSON
#include <nlohmann/json.hpp>
#include <unordered_map>

using namespace ncorr;
using namespace cppxdic;

DicAnalysis::DicAnalysis(const Config& config) : config_(config) {
}

bool DicAnalysis::dicDeformationAnalysis(const std::vector<int>& trial_target) {
    std::cout << "Starting Deformation/Strain Analysis (Step F)..." << std::endl;
    try {
        auto write_bin = [](const std::string& path, const std::vector<double>& buf){
            std::ofstream ofs(path, std::ios::binary);
            ofs.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()*sizeof(double)));
        };

        for (int trial : trial_target) {
            for (int pair = 1; pair <= config_.num_pair; ++pair) {
                // Recreate inputs (images + ROI) and load outputs per camera
                std::vector<std::string> cam1Frames; std::vector<std::string> cam2Frames;
                if (!Utils::importVid(config_, trial, pair, cam1Frames, cam2Frames) || cam1Frames.empty()) {
                    std::cerr << "No frames for strain analysis: trial " << trial << " pair " << pair << std::endl;
                    continue;
                }
                std::string roiJsonPath, roiMaskImagePath;
                Utils::loadROIFromMat(config_, trial, pair, roiJsonPath, roiMaskImagePath);

                // Helper to process a camera
                auto process_cam = [&](int cam_index, const std::vector<std::string>& frames){
                    try {
                        // Setup DIC input with ROI if available
                        DIC_analysis_input dic_input;
                        if (!roiMaskImagePath.empty()) {
                            if (!setupNcorrAnalysis(frames, roiMaskImagePath, dic_input)) return false;
                        } else {
                            if (!setupNcorrAnalysis(frames, dic_input)) return false;
                        }
                        // Load DIC output
                        std::ostringstream outbin;
                        outbin << config_.dic_path << "/" << config_.subject_id << "/" << config_.material
                               << "/dic_output_S" << config_.subject_id << "_" << config_.material << "_" << config_.phase_id
                               << "_T" << trial << "_pair" << pair << "_cam" << cam_index << ".bin";
                        auto dic_output = DIC_analysis_output::load(outbin.str());

                        // Run strain
                        auto s_in = strain_analysis_input(dic_input, dic_output, SUBREGION::CIRCLE, config_.subregion_radius);
                        auto s_out = strain_analysis(s_in);

                        // Save per-frame strain arrays
                        std::vector<std::string> eyy_bins, exx_bins, exy_bins;
                        const auto& strains = s_out.strains;
                        for (size_t fi=0; fi<strains.size(); ++fi) {
                            const auto& S = strains[fi];
                            const auto& EYY = S.get_eyy().get_array();
                            const auto& EXX = S.get_exx().get_array();
                            const auto& EXY = S.get_exy().get_array();
                            // Flatten to row-major doubles
                            std::vector<double> eyy_buf(EYY.size());
                            std::vector<double> exx_buf(EXX.size());
                            std::vector<double> exy_buf(EXY.size());
                            std::memcpy(eyy_buf.data(), EYY.get_pointer(), EYY.size()*sizeof(double));
                            std::memcpy(exx_buf.data(), EXX.get_pointer(), EXX.size()*sizeof(double));
                            std::memcpy(exy_buf.data(), EXY.get_pointer(), EXY.size()*sizeof(double));
                            std::ostringstream pe, px, py;
                            pe << config_.dic_path << "/" << config_.subject_id << "/" << config_.material
                               << "/strain_eyy_S" << config_.subject_id << "_" << config_.material << "_" << config_.phase_id
                               << "_T" << trial << "_pair" << pair << "_cam" << cam_index << "_f" << std::setw(6) << std::setfill('0') << (fi+1) << ".bin";
                            px << config_.dic_path << "/" << config_.subject_id << "/" << config_.material
                               << "/strain_exx_S" << config_.subject_id << "_" << config_.material << "_" << config_.phase_id
                               << "_T" << trial << "_pair" << pair << "_cam" << cam_index << "_f" << std::setw(6) << std::setfill('0') << (fi+1) << ".bin";
                            py << config_.dic_path << "/" << config_.subject_id << "/" << config_.material
                               << "/strain_exy_S" << config_.subject_id << "_" << config_.material << "_" << config_.phase_id
                               << "_T" << trial << "_pair" << pair << "_cam" << cam_index << "_f" << std::setw(6) << std::setfill('0') << (fi+1) << ".bin";
                            write_bin(pe.str(), eyy_buf);
                            write_bin(px.str(), exx_buf);
                            write_bin(py.str(), exy_buf);
                            eyy_bins.push_back(pe.str());
                            exx_bins.push_back(px.str());
                            exy_bins.push_back(py.str());
                        }

                        // Write strain JSON per camera
                        std::ostringstream jout;
                        jout << config_.dic_path << "/" << config_.subject_id << "/" << config_.material
                             << "/strain_output_S" << config_.subject_id << "_" << config_.material << "_" << config_.phase_id
                             << "_T" << trial << "_pair" << pair << "_cam" << cam_index << ".json";
                        nlohmann::json j;
                        j["subject"] = config_.subject_id;
                        j["material"] = config_.material;
                        j["phase"] = config_.phase_id;
                        j["trial"] = trial;
                        j["pair"] = pair;
                        j["camera"] = cam_index;
                        j["subregion"] = { {"shape","CIRCLE"}, {"radius", config_.subregion_radius} };
                        j["units"] = { {"name","mm"}, {"units_per_pixel", config_.units_per_pixel} };
                        j["frames"] = nlohmann::json::array();
                        for (size_t fi=0; fi<eyy_bins.size(); ++fi) {
                            nlohmann::json jf;
                            jf["f_index"] = static_cast<int>(fi+1);
                            jf["eyy_bin"] = eyy_bins[fi];
                            jf["exx_bin"] = exx_bins[fi];
                            jf["exy_bin"] = exy_bins[fi];
                            j["frames"].push_back(jf);
                        }
                        std::ofstream os(jout.str());
                        os << j.dump(2) << std::endl;
                        return true;
                    } catch (...) { return false; }
                };

                if (!process_cam(1, cam1Frames)) {
                    std::cerr << "Strain analysis failed for cam1 T" << trial << " pair " << pair << std::endl;
                    return false;
                }
                if (!cam2Frames.empty()) {
                    if (!process_cam(2, cam2Frames)) {
                        std::cerr << "Strain analysis failed for cam2 T" << trial << " pair " << pair << std::endl;
                        return false;
                    }
                }
                std::cout << "Strain analysis completed for trial " << trial << ", pair " << pair << std::endl;
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error in Deformation/Strain Analysis: " << e.what() << std::endl;
        return false;
    }
}
bool DicAnalysis::dic3DReconstruction(const std::vector<int>& trial_target) {
    std::cout << "Starting 3D Reconstruction (Step E)..." << std::endl;
    try {

        auto write_bin = [](const std::string& path, const std::vector<double>& buf){
            std::ofstream ofs(path, std::ios::binary);
            ofs.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()*sizeof(double)));
        };

        auto build_faces_from_roi = [](const ncorr::ROI2D& roi, std::vector<int>& faces, std::vector<int>& indexLUT, int& W, int& H){
            const auto& mask = roi.get_mask();
            H = static_cast<int>(mask.height());
            W = static_cast<int>(mask.width());
            indexLUT.assign(H*W, -1);
            int idx=0;
            for (int y=0;y<H;++y){
                for (int x=0;x<W;++x){
                    if (mask(y,x)) indexLUT[y*W+x]=idx++;
                }
            }
            for (int y=0;y<H-1;++y){
                for (int x=0;x<W-1;++x){
                    int a=indexLUT[y*W+x];
                    int b=indexLUT[y*W+(x+1)];
                    int c=indexLUT[(y+1)*W+x];
                    int d=indexLUT[(y+1)*W+(x+1)];
                    if (a>=0 && b>=0 && c>=0) { faces.push_back(a); faces.push_back(b); faces.push_back(c); }
                    if (b>=0 && c>=0 && d>=0) { faces.push_back(b); faces.push_back(d); faces.push_back(c); }
                }
            }
        };

        auto extract_points2D = [](const ncorr::Disp2D& disp, std::vector<double>& pts_xy, std::vector<int>& indexLUT, int W, int H){
            const auto& Au = disp.get_u().get_array();
            const auto& Av = disp.get_v().get_array();
            pts_xy.clear(); pts_xy.reserve(std::count_if(indexLUT.begin(), indexLUT.end(), [](int v){return v>=0;})*2);
            // Build in index order
            for (int y=0;y<H;++y){
                for (int x=0;x<W;++x){
                    int idx = indexLUT[y*W+x];
                    if (idx<0) continue;
                    double u = Au(y,x);
                    double v = Av(y,x);
                    double px = static_cast<double>(x) + u;
                    double py = static_cast<double>(y) + v;
                    pts_xy.resize(std::max((size_t)((idx+1)*2), pts_xy.size()));
                    pts_xy[idx*2+0] = px;
                    pts_xy[idx*2+1] = py;
                }
            }
        };

        //

        auto solve_3d = [](const std::vector<double>& L1, const std::vector<double>& L2, double x1, double y1, double x2, double y2){
            // Build A X = b, A is 4x3, b is 4
            auto rows = [&](const std::vector<double>& L, double x, double y){
                double l1=L[0],l2=L[1],l3=L[2],l4=L[3],l5=L[4],l6=L[5],l7=L[6],l8=L[7],l9=L[8],l10=L[9],l11=L[10];
                std::array<double,3> r1{l1 - x*l9, l2 - x*l10, l3 - x*l11};
                std::array<double,3> r2{l5 - y*l9, l6 - y*l10, l7 - y*l11};
                double b1 = x - l4;
                double b2 = y - l8;
                return std::tuple(r1,r2,b1,b2);
            };
            auto [r11,r12,b11,b12] = rows(L1,x1,y1);
            auto [r21,r22,b21,b22] = rows(L2,x2,y2);
            // Normal equations A^T A X = A^T b
            double ATA[3][3] = {{0}}; double ATb[3]={0};
            auto accum = [&](const std::array<double,3>& r, double b){
                for(int i=0;i<3;++i){ ATb[i]+=r[i]*b; for(int j=0;j<3;++j) ATA[i][j]+=r[i]*r[j]; }
            };
            accum(r11,b11); accum(r12,b12); accum(r21,b21); accum(r22,b22);
            // Solve 3x3 via Cramer's or Gaussian elimination
            // Gaussian elimination
            double A_[3][4] = {
                {ATA[0][0], ATA[0][1], ATA[0][2], ATb[0]},
                {ATA[1][0], ATA[1][1], ATA[1][2], ATb[1]},
                {ATA[2][0], ATA[2][1], ATA[2][2], ATb[2]}
            };
            for(int i=0;i<3;++i){
                // pivot
                int piv=i; for(int r=i+1;r<3;++r) if (std::fabs(A_[r][i])>std::fabs(A_[piv][i])) piv=r;
                if (piv!=i) for(int c=0;c<4;++c) std::swap(A_[i][c],A_[piv][c]);
                double diag = A_[i][i]; if (std::fabs(diag)<1e-12) continue; for(int c=i;c<4;++c) A_[i][c]/=diag;
                for(int r=0;r<3;++r){ if (r==i) continue; double f=A_[r][i]; for(int c=i;c<4;++c) A_[r][c]-=f*A_[i][c]; }
            }
            return std::array<double,3>{A_[0][3],A_[1][3],A_[2][3]};
        };

        for (int trial : trial_target) {
            for (int pair = 1; pair <= config_.num_pair; ++pair) {
                // Input DIC 2D outputs
                std::string cam1_bin = config_.dic_path + "/" + config_.subject_id + "/" + config_.material +
                    "/dic_output_S" + config_.subject_id + "_" + config_.material + "_" + config_.phase_id + "_T" + std::to_string(trial) + "_pair" + std::to_string(pair) + "_cam1.bin";
                std::string cam2_bin = config_.dic_path + "/" + config_.subject_id + "/" + config_.material +
                    "/dic_output_S" + config_.subject_id + "_" + config_.material + "_" + config_.phase_id + "_T" + std::to_string(trial) + "_pair" + std::to_string(pair) + "_cam2.bin";
                if (!std::filesystem::exists(cam1_bin) || !std::filesystem::exists(cam2_bin)) {
                    std::cerr << "Missing 2D outputs for trial " << trial << ", pair " << pair << ". Skipping." << std::endl;
                    continue;
                }

                // Locate calibration .mat files
                std::string calib_dir = config_.data_path + "/rawdata/" + config_.subject_id + "/speckles/" + config_.material + "/calibration/";
                std::string calib_cam1, calib_cam2;
                if (std::filesystem::exists(calib_dir)) {
                    for (const auto& entry : std::filesystem::directory_iterator(calib_dir)) {
                        if (!entry.is_regular_file()) continue;
                        auto name = entry.path().filename().string();
                        std::string lower = name; std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                        if (lower.find("cam1") != std::string::npos || lower.find("camera1") != std::string::npos) calib_cam1 = entry.path().string();
                        if (lower.find("cam2") != std::string::npos || lower.find("camera2") != std::string::npos) calib_cam2 = entry.path().string();
                        if (!calib_cam1.empty() && !calib_cam2.empty()) break;
                    }
                }

                // Read DLT parameters from calibration MAT files (match Matlab field names)
                auto read_dltparams = [&](const std::string& matpath)->std::vector<double> {
                    std::vector<double> L;
                    if (matpath.empty()) return L;
                    mat_t *fp = Mat_Open(matpath.c_str(), MAT_ACC_RDONLY);
                    if (!fp) return L;
                    // Expect variable 'DLTstructCam' with field 'DLTparams'
                    matvar_t *st = Mat_VarRead(fp, "DLTstructCam");
                    if (st && st->class_type == MAT_C_STRUCT) {
                        matvar_t *field = Mat_VarGetStructFieldByName(st, "DLTparams", 0);
                        if (field && field->data && field->class_type == MAT_C_DOUBLE) {
                            size_t n = 1;
                            for (int i=0;i<field->rank;i++) n *= field->dims[i];
                            const double* d = static_cast<const double*>(field->data);
                            L.assign(d, d + n);
                        }
                    }
                    if (st) Mat_VarFree(st);
                    Mat_Close(fp);
                    return L;
                };

                std::vector<double> L1 = read_dltparams(calib_cam1);
                std::vector<double> L2 = read_dltparams(calib_cam2);
                // Persist a calibration.json for traceability
                try {
                    std::ostringstream cjson;
                    cjson << config_.dic_path << "/" << config_.subject_id << "/" << config_.material << "/calibration.json";
                    nlohmann::json jc;
                    jc["subject"] = config_.subject_id;
                    jc["material"] = config_.material;
                    jc["phase"] = config_.phase_id;
                    jc["cam1_mat"] = calib_cam1;
                    jc["cam2_mat"] = calib_cam2;
                    if (!L1.empty()) jc["DLTparameters"]["cam1"] = L1;
                    if (!L2.empty()) jc["DLTparameters"]["cam2"] = L2;
                    std::ofstream oc(cjson.str());
                    oc << jc.dump(2) << std::endl;
                } catch (...) {}

                // Load DIC outputs
                auto dic1 = DIC_analysis_output::load(cam1_bin);
                auto dic2 = DIC_analysis_output::load(cam2_bin);
                if (dic1.disps.size() != dic2.disps.size()) {
                    std::cerr << "Cam1/Cam2 frame count mismatch for trial " << trial << " pair " << pair << std::endl;
                    continue;
                }

                // Build faces and index LUT from reference ROI (use frame 0 from cam1)
                std::vector<int> faces; std::vector<int> indexLUT; int W=0,H=0;
                build_faces_from_roi(dic1.disps.front().get_roi(), faces, indexLUT, W, H);

                // Prepare outputs per frame
                std::vector<std::string> points3d_bins;
                std::vector<std::string> dispvec_bins;
                std::vector<std::string> dispmgn_bins;

                std::vector<double> P3D_ref; // frame 1 reference

                for (size_t fi=0; fi<dic1.disps.size(); ++fi) {
                    const auto& d1 = dic1.disps[fi];
                    const auto& d2 = dic2.disps[fi];
                    std::vector<double> pts1, pts2;
                    extract_points2D(d1, pts1, indexLUT, W, H);
                    extract_points2D(d2, pts2, indexLUT, W, H);
                    size_t N = pts1.size()/2;
                    std::vector<double> pts3d; pts3d.resize(N*3, std::numeric_limits<double>::quiet_NaN());
                    for (size_t k=0; k<N; ++k){
                        double x1 = pts1[k*2+0], y1 = pts1[k*2+1];
                        double x2 = pts2[k*2+0], y2 = pts2[k*2+1];
                        if (L1.size()>=11 && L2.size()>=11) {
                            auto X = solve_3d(L1,L2,x1,y1,x2,y2);
                            pts3d[k*3+0]=X[0]; pts3d[k*3+1]=X[1]; pts3d[k*3+2]=X[2];
                        }
                    }
                    // Save points3d
                    std::ostringstream p3d;
                    p3d << config_.dic_path << "/" << config_.subject_id << "/" << config_.material
                        << "/points3d_S" << config_.subject_id << "_" << config_.material << "_" << config_.phase_id
                        << "_T" << trial << "_pair" << pair << "_f" << std::setw(6) << std::setfill('0') << (fi+1) << ".bin";
                    write_bin(p3d.str(), pts3d);
                    points3d_bins.push_back(p3d.str());

                    // Compute displacement from frame 1
                    if (fi==0) P3D_ref = pts3d;
                    std::vector<double> dispvec; dispvec.resize(N*3, std::numeric_limits<double>::quiet_NaN());
                    std::vector<double> dispmgn; dispmgn.resize(N, std::numeric_limits<double>::quiet_NaN());
                    for (size_t k=0;k<N;++k){
                        double dx = pts3d[k*3+0]-P3D_ref[k*3+0];
                        double dy = pts3d[k*3+1]-P3D_ref[k*3+1];
                        double dz = pts3d[k*3+2]-P3D_ref[k*3+2];
                        dispvec[k*3+0]=dx; dispvec[k*3+1]=dy; dispvec[k*3+2]=dz;
                        dispmgn[k]=std::sqrt(dx*dx+dy*dy+dz*dz);
                    }
                    std::ostringstream dvb, dmb;
                    dvb << config_.dic_path << "/" << config_.subject_id << "/" << config_.material
                        << "/dispvec_S" << config_.subject_id << "_" << config_.material << "_" << config_.phase_id
                        << "_T" << trial << "_pair" << pair << "_f" << std::setw(6) << std::setfill('0') << (fi+1) << ".bin";
                    dmb << config_.dic_path << "/" << config_.subject_id << "/" << config_.material
                        << "/dispmgn_S" << config_.subject_id << "_" << config_.material << "_" << config_.phase_id
                        << "_T" << trial << "_pair" << pair << "_f" << std::setw(6) << std::setfill('0') << (fi+1) << ".bin";
                    write_bin(dvb.str(), dispvec);
                    write_bin(dmb.str(), dispmgn);
                    dispvec_bins.push_back(dvb.str());
                    dispmgn_bins.push_back(dmb.str());
                }

                std::ostringstream out3d;
                out3d << config_.dic_path << "/" << config_.subject_id << "/" << config_.material
                      << "/dic3d_output_S" << config_.subject_id << "_" << config_.material << "_" << config_.phase_id
                      << "_T" << trial << "_pair" << pair << ".json";
                nlohmann::json j;
                j["subject"] = config_.subject_id;
                j["material"] = config_.material;
                j["phase"] = config_.phase_id;
                j["trial"] = trial;
                j["pair"] = pair;
                j["inputs"] = {
                    {"cam1", cam1_bin},
                    {"cam2", cam2_bin}
                };
                j["calibration"]["cam1_mat"] = calib_cam1;
                j["calibration"]["cam2_mat"] = calib_cam2;
                if (!L1.empty()) j["calibration"]["DLTparameters"]["cam1"] = L1;
                if (!L2.empty()) j["calibration"]["DLTparameters"]["cam2"] = L2;
                // Faces
                j["Faces_width"] = W;
                j["Faces_height"] = H;
                // Save faces as a binary triplet list
                std::ostringstream fbin;
                fbin << config_.dic_path << "/" << config_.subject_id << "/" << config_.material
                     << "/faces_S" << config_.subject_id << "_" << config_.material << "_" << config_.phase_id
                     << "_T" << trial << "_pair" << pair << ".bin";
                std::vector<double> faces_d; faces_d.reserve(faces.size()); for (int v:faces) faces_d.push_back(static_cast<double>(v));
                write_bin(fbin.str(), faces_d);
                j["Faces_bin"] = fbin.str();
                // Per-frame binaries
                j["frames"] = nlohmann::json::array();
                for (size_t fi=0; fi<points3d_bins.size(); ++fi){
                    nlohmann::json jf;
                    jf["f_index"] = static_cast<int>(fi+1);
                    jf["points3d_bin"] = points3d_bins[fi];
                    jf["dispvec_bin"] = dispvec_bins[fi];
                    jf["dispmgn_bin"] = dispmgn_bins[fi];
                    j["frames"].push_back(jf);
                }
                j["status"] = "ok";
                std::ofstream ofs(out3d.str());
                ofs << j.dump(2) << std::endl;

                std::cout << "Wrote 3D placeholder for trial " << trial << ", pair " << pair << std::endl;
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error in 3D Reconstruction: " << e.what() << std::endl;
        return false;
    }
}

bool DicAnalysis::setupNcorrAnalysis(const std::vector<std::string>& images,
                                    const std::string& roi_mask_image_path,
                                    DIC_analysis_input& dic_input) {
    try {
        if (images.size() < 2) {
            std::cerr << "Need at least 2 images for DIC analysis" << std::endl;
            return false;
        }

        std::vector<Image2D> ncorr_images;
        for (const auto& img_path : images) {
            ncorr_images.emplace_back(img_path);
        }

        // Build ROI from provided mask image (non-zero pixels inside ROI)
        Image2D roi_mask(roi_mask_image_path);
        ROI2D roi(roi_mask.get_gs() > 0.5);

        dic_input = DIC_analysis_input(
            ncorr_images,
            roi,
            3,
            INTERP::QUINTIC_BSPLINE_PRECOMPUTE,
            SUBREGION::CIRCLE,
            config_.subregion_radius,
            4,
            DIC_analysis_config::NO_UPDATE,
            config_.debug_mode
        );

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Error setting up ncorr analysis with ROI mask: " << e.what() << std::endl;
        return false;
    }
}

bool DicAnalysis::run() {
    // Search for trial targets (equivalent to search_trial2target)
    std::vector<int> trial_target = searchTrialTarget();
    
    std::cout << "Trial target set: [";
    for (size_t i = 0; i < trial_target.size(); ++i) {
        std::cout << trial_target[i];
        if (i < trial_target.size() - 1) std::cout << ", ";
    }
    std::cout << "]" << std::endl;
    
    // STEP D: 2D-DIC
    auto start = std::chrono::high_resolution_clock::now();
    bool success = dic2DAnalysis(trial_target);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    if (!success) {
        std::cerr << "2D DIC Analysis failed!" << std::endl;
        return false;
    }
    
    std::cout << "DIC 2D Analysis done in " << duration.count() / 1000.0 << " s" << std::endl;
    
    // STEP E: 3D Reconstruction
    start = std::chrono::high_resolution_clock::now();
    success = dic3DReconstruction(trial_target);
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    if (!success) {
        std::cerr << "3D Reconstruction failed!" << std::endl;
        return false;
    }
    
    std::cout << "DIC 3D Reconstruction done in " << duration.count() / 1000.0 << " s" << std::endl;
    
    // STEP F: Deformation analysis
    start = std::chrono::high_resolution_clock::now();
    success = dicDeformationAnalysis(trial_target);
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    if (!success) {
        std::cerr << "Deformation Analysis failed!" << std::endl;
        return false;
    }
    
    std::cout << "DIC Deformation Analysis done in " << duration.count() / 1000.0 << " s" << std::endl;
    
    return true;
}

std::vector<int> DicAnalysis::searchTrialTarget() {
    std::vector<int> trials;

    // Build protocol directory path
    std::string protocol_dir = config_.data_path + "/rawdata/" + config_.subject_id +
                               "/speckles/" + config_.material + "/protocol/";

    // Find protocol .mat file
    auto protos = Utils::findFiles(protocol_dir, "*.mat");
    if (protos.empty()) {
        std::cerr << "Protocol file not found in: " << protocol_dir << std::endl;
        // Fallback to reference + next
        trials.push_back(config_.ref_trial_id);
        trials.push_back(config_.ref_trial_id + 1);
        return trials;
    }

    std::string proto_file = protos.front();

    // Open MAT file
    mat_t *matfp = Mat_Open(proto_file.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        std::cerr << "Failed to open MAT file: " << proto_file << std::endl;
        trials.push_back(config_.ref_trial_id);
        trials.push_back(config_.ref_trial_id + 1);
        return trials;
    }

    // Load 'cond' struct
    matvar_t *cond = Mat_VarRead(matfp, "cond");
    if (!cond || cond->class_type != MAT_C_STRUCT) {
        if (cond) Mat_VarFree(cond);
        Mat_Close(matfp);
        std::cerr << "Variable 'cond' not found or not a struct in: " << proto_file << std::endl;
        trials.push_back(config_.ref_trial_id);
        trials.push_back(config_.ref_trial_id + 1);
        return trials;
    }

    // Read titles (cell array of strings)
    matvar_t *titles = Mat_VarGetStructFieldByName(cond, "titles", 0);
    matvar_t *table = Mat_VarGetStructFieldByName(cond, "table", 0);
    if (!titles || titles->class_type != MAT_C_CELL || !table || table->class_type != MAT_C_CELL) {
        if (cond) Mat_VarFree(cond);
        Mat_Close(matfp);
        std::cerr << "Fields 'titles' or 'table' missing or of wrong type in 'cond'" << std::endl;
        trials.push_back(config_.ref_trial_id);
        trials.push_back(config_.ref_trial_id + 1);
        return trials;
    }

    // Extract column names
    std::vector<std::string> col_names;
    size_t ncols = titles->dims[1];
    col_names.reserve(ncols);
    for (size_t j = 0; j < ncols; ++j) {
        matvar_t *cell = static_cast<matvar_t **>(titles->data)[j];
        std::string name;
        if (cell && cell->class_type == MAT_C_CHAR && cell->data) {
            size_t len = cell->nbytes / cell->data_size;
            name.assign(static_cast<const char *>(cell->data), len);
            // Titles might have trailing nulls; trim
            while (!name.empty() && (name.back() == '\0' || name.back() == ' ')) name.pop_back();
        }
        col_names.push_back(name);
    }

    // Identify indices for 'dir', 'nf', 'spddxl'
    auto find_col = [&](const std::string &key) -> int {
        for (size_t j = 0; j < col_names.size(); ++j) {
            if (col_names[j] == key) return static_cast<int>(j);
        }
        return -1;
    };
    int dir_idx = find_col("dir");
    int nf_idx = find_col("nf");
    int spd_idx = find_col("spddxl");
    if (dir_idx < 0 || nf_idx < 0 || spd_idx < 0) {
        if (cond) Mat_VarFree(cond);
        Mat_Close(matfp);
        std::cerr << "Required columns not found in titles (need 'dir','nf','spddxl')" << std::endl;
        trials.push_back(config_.ref_trial_id);
        trials.push_back(config_.ref_trial_id + 1);
        return trials;
    }

    // Table dimensions: Ntrial x Ncond
    size_t ntrial = table->dims[0];
    size_t ncond = table->dims[1];

    // Pre-extract columns from cell table
    auto cell_at = [&](size_t i, size_t j) -> matvar_t * {
        size_t idx = i + j * ntrial; // column-major
        return static_cast<matvar_t **>(table->data)[idx];
    };

    std::vector<std::string> dircol(ntrial);
    std::vector<double> nfcol(ntrial, std::numeric_limits<double>::quiet_NaN());
    std::vector<double> spdcol(ntrial, std::numeric_limits<double>::quiet_NaN());

    for (size_t i = 0; i < ntrial; ++i) {
        // dir as string
        if (dir_idx < static_cast<int>(ncond)) {
            matvar_t *c = cell_at(i, static_cast<size_t>(dir_idx));
            if (c && c->class_type == MAT_C_CHAR && c->data) {
                size_t len = c->nbytes / c->data_size;
                std::string s(static_cast<const char *>(c->data), len);
                while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
                dircol[i] = s;
            }
        }
        // nf numeric
        if (nf_idx < static_cast<int>(ncond)) {
            matvar_t *c = cell_at(i, static_cast<size_t>(nf_idx));
            if (c && c->data) {
                if (c->class_type == MAT_C_DOUBLE) {
                    nfcol[i] = static_cast<const double *>(c->data)[0];
                } else if (c->class_type == MAT_C_SINGLE) {
                    nfcol[i] = static_cast<const float *>(c->data)[0];
                } else if (c->class_type == MAT_C_INT32) {
                    nfcol[i] = static_cast<const int32_t *>(c->data)[0];
                }
            }
        }
        // spddxl numeric
        if (spd_idx < static_cast<int>(ncond)) {
            matvar_t *c = cell_at(i, static_cast<size_t>(spd_idx));
            if (c && c->data) {
                if (c->class_type == MAT_C_DOUBLE) {
                    spdcol[i] = static_cast<const double *>(c->data)[0];
                } else if (c->class_type == MAT_C_SINGLE) {
                    spdcol[i] = static_cast<const float *>(c->data)[0];
                } else if (c->class_type == MAT_C_INT32) {
                    spdcol[i] = static_cast<const int32_t *>(c->data)[0];
                }
            }
        }
    }

    // Subject numeric id
    int subj_num = 0;
    {
        // extract digits from subject_id
        for (char ch : config_.subject_id) {
            if (std::isdigit(static_cast<unsigned char>(ch))) {
                subj_num = subj_num * 10 + (ch - '0');
            }
        }
    }

    // Correction if subject < 8
    if (subj_num < 8 && ntrial > 1) {
        size_t half = ntrial / 2;
        for (size_t i = 0; i < ntrial; ++i) {
            spdcol[i] = (i < half) ? 0.04 : 0.08;
        }
    }

    // Build trial indices 1..Ntrial (Matlab-style) based on filters

    bool is_loading = (config_.phase_id == "loading");
    std::vector<int> trialnum;
    trialnum.reserve(ntrial);
    for (size_t i = 0; i < ntrial; ++i) trialnum.push_back(static_cast<int>(i + 1));

    for (size_t ii = 0; ii < config_.nfcond_set.size(); ++ii) {
        int nf_set = config_.nfcond_set[ii];
        for (size_t jj = 0; jj < config_.spddxlcond_set.size(); ++jj) {
            double spd_set = config_.spddxlcond_set[jj];

            for (size_t i = 0; i < ntrial; ++i) {
                bool pass = true;
                if (is_loading) {
                    bool dir_ok = (dircol[i] == "Ubnf" || dircol[i] == "Rbnf");
                    bool nf_ok = std::fabs(nfcol[i] - nf_set) < 1e-6;
                    bool spd_ok = std::fabs(spdcol[i] - spd_set) < 1e-9;
                    pass = dir_ok && nf_ok && spd_ok;
                }
                if (pass) trials.push_back(trialnum[i]);
            }
        }
    }

    // Clean up
    if (cond) Mat_VarFree(cond);
    Mat_Close(matfp);

    return trials;
}

bool DicAnalysis::dic2DAnalysis(const std::vector<int>& trial_target) {
    std::cout << "Starting 2D DIC Analysis (using StepDWorkflow)..." << std::endl;
    
    try {
        // Create workflow instance
        StepDWorkflow workflow(config_);
        
        // Process each trial and stereo pair
        for (int trial : trial_target) {
            for (int pair = 1; pair <= config_.num_pair; ++pair) {
                // Format trial as 3-digit string (e.g., "005")
                std::ostringstream trial_str;
                trial_str << std::setw(3) << std::setfill('0') << trial;
                
                std::cout << "\n========================================" << std::endl;
                std::cout << "Processing Trial " << trial << ", Pair " << pair << std::endl;
                std::cout << "========================================" << std::endl;
                
                // Execute workflow
                auto [outputPath, pairOrder, pairForced] = workflow.execute(trial_str.str(), pair);
                
                if (outputPath.empty()) {
                    std::cerr << "Workflow failed for trial " << trial << ", pair " << pair << std::endl;
                    return false;
                }
                
                std::cout << "✓ Trial " << trial << ", pair " << pair << " completed successfully." << std::endl;
                std::cout << "  Output path: " << outputPath << std::endl;
                std::cout << "  Pair order: [" << pairOrder[0] << ", " << pairOrder[1] << "]" << std::endl;
                std::cout << "  Pair forced: " << (pairForced ? "true" : "false") << std::endl;
            }
        }
        
        std::cout << "\n✓ All 2D DIC Analysis completed successfully!" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in 2D DIC Analysis: " << e.what() << std::endl;
        return false;
    }
}


std::vector<std::string> DicAnalysis::loadImageSequence(const std::string& trial_path) {
    std::vector<std::string> images;
    
    // Look for common image formats
    std::vector<std::string> extensions = {".png", ".jpg", ".jpeg", ".tiff", ".bmp"};
    
    try {
        if (std::filesystem::exists(trial_path)) {
            for (const auto& entry : std::filesystem::directory_iterator(trial_path)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().string();
                    for (const auto& ext : extensions) {
                        if (filename.size() >= ext.size() && 
                            filename.compare(filename.size() - ext.size(), ext.size(), ext) == 0) {
                            images.push_back(filename);
                            break;
                        }
                    }
                }
            }
        }
        
        // Sort images to ensure proper sequence
        std::sort(images.begin(), images.end());
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading image sequence: " << e.what() << std::endl;
    }
    
    return images;
}

bool DicAnalysis::setupNcorrAnalysis(const std::vector<std::string>& images, 
                                   DIC_analysis_input& dic_input) {
    try {
        if (images.size() < 2) {
            std::cerr << "Need at least 2 images for DIC analysis" << std::endl;
            return false;
        }
        
        // Convert string paths to Image2D objects
        std::vector<Image2D> ncorr_images;
        for (const auto& img_path : images) {
            ncorr_images.emplace_back(img_path);
        }
        
        Image2D first_image(images[0]);
        ROI2D roi(first_image.get_gs() > 0.1);
        
        // Setup DIC input with parameters similar to the ncorr test
        dic_input = DIC_analysis_input(
            ncorr_images,                                    // Images
            roi,                                            // ROI
            3,                                              // scalefactor
            INTERP::QUINTIC_BSPLINE_PRECOMPUTE,            // Interpolation
            SUBREGION::CIRCLE,                             // Subregion shape
            config_.subregion_radius,                      // Subregion radius
            4,                                             // # of threads
            DIC_analysis_config::NO_UPDATE,                // DIC configuration
            config_.debug_mode                             // Debugging enabled/disabled
        );
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error setting up ncorr analysis: " << e.what() << std::endl;
        return false;
    }
}
