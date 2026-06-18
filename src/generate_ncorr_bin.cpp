/**
 * generate_ncorr_bin - Convert MATLAB ncorr .mat files to ncorr native .bin format
 *
 * Reads a MATLAB ncorr output file (HDF5 v7.3) such as ncorr1.mat, ncorr12.mat, etc.
 * and produces the equivalent .bin file that the C++ pipeline expects for Step E/F.
 *
 * Usage:
 *   generate_ncorr_bin <input.mat> [output.bin]
 *
 * If output.bin is not specified, it defaults to replacing .mat with .bin in the input path.
 *
 * The .mat file must contain:
 *   data_dic_save/dispinfo/spacing        (scalar)
 *   data_dic_save/dispinfo/pixtounits     (scalar)
 *   data_dic_save/dispinfo/units          (string)
 *   data_dic_save/displacements/plot_u_dic       (object ref array of 2D double)
 *   data_dic_save/displacements/plot_v_dic       (object ref array of 2D double)
 *   data_dic_save/displacements/plot_corrcoef_dic (object ref array of 2D double)
 *   data_dic_save/displacements/roi_dic           (object ref array of 2D logical/uint8)
 */

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <cmath>

#include <matio.h>
#include <hdf5.h>

#include "ncorr.h"
#include "mat_reader.h"
#include "logging.h"

using namespace ncorr;
using namespace cppxdic;

// ---------------------------------------------------------------------------
// Helpers for reading nested HDF5 groups as matio structs
// ---------------------------------------------------------------------------

static matvar_t* getField(matvar_t* parent, const char* name) {
    if (!parent || parent->class_type != MAT_C_STRUCT) return nullptr;
    return Mat_VarGetStructFieldByName(parent, name, 0);
}

// ---------------------------------------------------------------------------
// HDF5 helpers for reading object reference arrays (MATLAB cell arrays)
// ---------------------------------------------------------------------------

// Read a 2D double dataset from an HDF5 object id (column-major → Array2D row-major)
static Array2D<double> h5ReadDoubleArray2D(hid_t file_id, const hobj_ref_t& ref) {
    hid_t obj_id = H5Rdereference(file_id, H5P_DEFAULT, H5R_OBJECT, &ref);
    if (obj_id < 0) return {};

    hid_t space = H5Dget_space(obj_id);
    int ndims = H5Sget_simple_extent_ndims(space);
    if (ndims != 2) {
        H5Sclose(space);
        H5Dclose(obj_id);
        return {};
    }

    hsize_t dims[2];
    H5Sget_simple_extent_dims(space, dims, nullptr);
    H5Sclose(space);

    // HDF5/MATLAB: dims[0] = cols, dims[1] = rows (column-major storage)
    const size_t cols = dims[0];
    const size_t rows = dims[1];

    std::vector<double> buf(rows * cols);
    herr_t err = H5Dread(obj_id, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, buf.data());
    H5Dclose(obj_id);

    if (err < 0) return {};

    Array2D<double> arr(static_cast<std::ptrdiff_t>(rows),
                        static_cast<std::ptrdiff_t>(cols));
    // buf is column-major: buf[c * rows + r]
    for (size_t c = 0; c < cols; ++c)
        for (size_t r = 0; r < rows; ++r)
            arr(static_cast<std::ptrdiff_t>(r),
                static_cast<std::ptrdiff_t>(c)) = buf[c * rows + r];
    return arr;
}

// Read a 2D uint8/logical mask from an HDF5 object ref → Array2D<bool>
static Array2D<bool> h5ReadMask2D(hid_t file_id, const hobj_ref_t& ref) {
    hid_t obj_id = H5Rdereference(file_id, H5P_DEFAULT, H5R_OBJECT, &ref);
    if (obj_id < 0) return {};

    hid_t space = H5Dget_space(obj_id);
    int ndims = H5Sget_simple_extent_ndims(space);
    if (ndims != 2) {
        H5Sclose(space);
        H5Dclose(obj_id);
        return {};
    }

    hsize_t dims[2];
    H5Sget_simple_extent_dims(space, dims, nullptr);
    H5Sclose(space);

    const size_t cols = dims[0];
    const size_t rows = dims[1];

    // Determine native type
    hid_t dtype = H5Dget_type(obj_id);
    H5T_class_t tclass = H5Tget_class(dtype);
    H5Tclose(dtype);

    Array2D<bool> mask(static_cast<std::ptrdiff_t>(rows),
                       static_cast<std::ptrdiff_t>(cols), false);

    if (tclass == H5T_INTEGER) {
        std::vector<uint8_t> buf(rows * cols);
        H5Dread(obj_id, H5T_NATIVE_UINT8, H5S_ALL, H5S_ALL, H5P_DEFAULT, buf.data());
        for (size_t c = 0; c < cols; ++c)
            for (size_t r = 0; r < rows; ++r)
                mask(static_cast<std::ptrdiff_t>(r),
                     static_cast<std::ptrdiff_t>(c)) = (buf[c * rows + r] != 0);
    } else if (tclass == H5T_FLOAT) {
        std::vector<double> buf(rows * cols);
        H5Dread(obj_id, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, buf.data());
        for (size_t c = 0; c < cols; ++c)
            for (size_t r = 0; r < rows; ++r)
                mask(static_cast<std::ptrdiff_t>(r),
                     static_cast<std::ptrdiff_t>(c)) = (buf[c * rows + r] != 0.0);
    } else {
        LOG_WARN << "unsupported HDF5 type class " << tclass << " for ROI mask";
    }

    H5Dclose(obj_id);
    return mask;
}

// Read an object reference array dataset, return vector of hobj_ref_t and count
static std::vector<hobj_ref_t> h5ReadObjRefArray(hid_t file_id, const char* path, size_t& count) {
    count = 0;
    hid_t dset = H5Dopen2(file_id, path, H5P_DEFAULT);
    if (dset < 0) return {};

    hid_t space = H5Dget_space(dset);
    hssize_t nElems = H5Sget_simple_extent_npoints(space);
    H5Sclose(space);

    if (nElems <= 0) {
        H5Dclose(dset);
        return {};
    }

    count = static_cast<size_t>(nElems);
    std::vector<hobj_ref_t> refs(count);
    herr_t err = H5Dread(dset, H5T_STD_REF_OBJ, H5S_ALL, H5S_ALL, H5P_DEFAULT, refs.data());
    H5Dclose(dset);

    if (err < 0) {
        count = 0;
        return {};
    }
    return refs;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: generate_ncorr_bin <input.mat> [output.bin]\n";
        return 1;
    }

    std::string input_path = argv[1];
    std::string output_path;
    if (argc >= 3) {
        output_path = argv[2];
    } else {
        // Replace .mat with .bin
        output_path = input_path;
        auto pos = output_path.rfind(".mat");
        if (pos != std::string::npos) {
            output_path.replace(pos, 4, ".bin");
        } else {
            output_path += ".bin";
        }
    }

    if (!std::filesystem::exists(input_path)) {
        LOG_ERROR << "input file not found: " << input_path;
        return 1;
    }

    LOG_INFO << "Input:  " << input_path;
    LOG_INFO << "Output: " << output_path;

    // -----------------------------------------------------------------------
    // Open MAT file via matio (for dispinfo scalars/strings)
    // -----------------------------------------------------------------------
    auto matfp = MatReader::openMat(input_path);
    if (!matfp) {
        LOG_ERROR << "failed to open MAT file: " << input_path;
        return 1;
    }

    auto data_dic_save = MatReader::readVar(matfp.get(), "data_dic_save");
    if (!data_dic_save || data_dic_save->class_type != MAT_C_STRUCT) {
        LOG_ERROR << "'data_dic_save' not found or not a struct";
        return 1;
    }

    // -----------------------------------------------------------------------
    // Read dispinfo via matio (works fine for scalars/strings)
    // -----------------------------------------------------------------------
    matvar_t* dispinfo = getField(data_dic_save.get(), "dispinfo");
    if (!dispinfo || dispinfo->class_type != MAT_C_STRUCT) {
        LOG_ERROR << "'data_dic_save.dispinfo' not found";
        return 1;
    }

    int spacing = 0;
    if (matvar_t* f = getField(dispinfo, "spacing")) {
        spacing = static_cast<int>(MatReader::readScalar(f));
    }
    const std::ptrdiff_t scalefactor = spacing + 1;

    double pixtounits = 1.0;
    if (matvar_t* f = getField(dispinfo, "pixtounits")) {
        pixtounits = MatReader::readScalar(f);
    }

    std::string units = "pixels";
    if (matvar_t* f = getField(dispinfo, "units")) {
        units = MatReader::readString(f);
    }

    std::string type_str = "Lagrangian";
    if (matvar_t* f = getField(dispinfo, "type")) {
        type_str = MatReader::readString(f);
    }

    PERSPECTIVE perspective = PERSPECTIVE::LAGRANGIAN;
    if (type_str.find("ulerian") != std::string::npos ||
        type_str.find("Eulerian") != std::string::npos) {
        perspective = PERSPECTIVE::EULERIAN;
    }

    LOG_INFO << "  spacing=" << spacing << " (scalefactor=" << scalefactor << ")";
    LOG_INFO << "  pixtounits=" << pixtounits;
    LOG_INFO << "  units=\"" << units << "\"";
    LOG_INFO << "  type=\"" << type_str << "\"";

    // Close matio handle before opening HDF5 directly
    data_dic_save.reset();
    matfp.reset();

    // -----------------------------------------------------------------------
    // Open as HDF5 for reading object reference arrays
    // -----------------------------------------------------------------------
    hid_t file_id = H5Fopen(input_path.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file_id < 0) {
        LOG_ERROR << "failed to open HDF5 file: " << input_path;
        return 1;
    }

    const char* u_path  = "/data_dic_save/displacements/plot_u_dic";
    const char* v_path  = "/data_dic_save/displacements/plot_v_dic";
    const char* cc_path = "/data_dic_save/displacements/plot_corrcoef_dic";
    const char* roi_path = "/data_dic_save/displacements/roi_dic";

    size_t nFrames = 0;
    auto u_refs = h5ReadObjRefArray(file_id, u_path, nFrames);
    if (nFrames == 0) {
        LOG_ERROR << "plot_u_dic has 0 references";
        H5Fclose(file_id);
        return 1;
    }
    LOG_INFO << "  nFrames=" << nFrames;

    size_t nV = 0, nCC = 0, nROI = 0;
    auto v_refs   = h5ReadObjRefArray(file_id, v_path, nV);
    auto cc_refs  = h5ReadObjRefArray(file_id, cc_path, nCC);
    auto roi_refs = h5ReadObjRefArray(file_id, roi_path, nROI);

    if (nV != nFrames) {
        LOG_ERROR << "plot_v_dic has " << nV << " refs (expected " << nFrames << ")";
        H5Fclose(file_id);
        return 1;
    }

    // -----------------------------------------------------------------------
    // Probe ROI: roi_dic refs point to ncorr_class_roi objects (HDF5 structs),
    // not simple 2D boolean masks. Try to read the first one; if it fails or
    // has wrong dimensions, fall back to all-true masks for every frame.
    // -----------------------------------------------------------------------
    bool roi_usable = false;
    if (nROI > 0) {
        // Peek at first ROI reference to check if it's a plain 2D mask
        Array2D<bool> test_mask = h5ReadMask2D(file_id, roi_refs[0]);
        // We'll know the expected dims from the first u frame
        Array2D<double> u0 = h5ReadDoubleArray2D(file_id, u_refs[0]);
        if (!test_mask.empty() && !u0.empty() &&
            test_mask.height() == u0.height() && test_mask.width() == u0.width()) {
            roi_usable = true;
        }
    }
    if (!roi_usable && nROI > 0) {
        LOG_INFO << "  Note: roi_dic contains ncorr_class_roi objects (not simple masks). "
                 << "Using all-true ROI masks — non-ROI pixels have zero displacement.";
    }

    // -----------------------------------------------------------------------
    // Build DIC_analysis_output
    // -----------------------------------------------------------------------
    std::vector<Disp2D> disps;
    disps.reserve(nFrames);

    size_t skipped = 0;
    for (size_t f = 0; f < nFrames; ++f) {
        Array2D<double> u_arr = h5ReadDoubleArray2D(file_id, u_refs[f]);
        Array2D<double> v_arr = h5ReadDoubleArray2D(file_id, v_refs[f]);

        if (u_arr.empty() || v_arr.empty()) {
            LOG_WARN << "frame " << f << " has empty u or v array, skipping";
            ++skipped;
            disps.emplace_back();
            continue;
        }

        // Correlation coefficient
        Array2D<double> cc_arr;
        if (f < nCC) {
            cc_arr = h5ReadDoubleArray2D(file_id, cc_refs[f]);
        }
        if (cc_arr.empty()) {
            cc_arr = Array2D<double>(u_arr.height(), u_arr.width(), 0.0);
        }

        // ROI mask
        Array2D<bool> mask;
        if (roi_usable && f < nROI) {
            mask = h5ReadMask2D(file_id, roi_refs[f]);
        }
        if (mask.empty() || mask.height() != u_arr.height() || mask.width() != u_arr.width()) {
            mask = Array2D<bool>(u_arr.height(), u_arr.width(), true);
        }

        // Validate u/v dimensions
        if (u_arr.height() != v_arr.height() || u_arr.width() != v_arr.width()) {
            LOG_WARN << "frame " << f << " u/v dimension mismatch ("
                     << u_arr.height() << "x" << u_arr.width() << " vs "
                     << v_arr.height() << "x" << v_arr.width() << ")";
            ++skipped;
            disps.emplace_back();
            continue;
        }

        ROI2D roi(std::move(mask));
        disps.emplace_back(std::move(v_arr), std::move(u_arr), std::move(cc_arr),
                           roi, scalefactor);

        if (f == 0) {
            LOG_INFO << "  Frame 0 size: "
                     << disps.back().data_height() << "x"
                     << disps.back().data_width()
                     << ", ROI regions: " << disps.back().get_roi().size_regions()
                     << ", scalefactor: " << disps.back().get_scalefactor();
        }
    }

    H5Fclose(file_id);

    LOG_INFO << "  Built " << disps.size() << " displacement frames ("
             << skipped << " skipped)";

    DIC_analysis_output output(disps, perspective, units, pixtounits);

    // -----------------------------------------------------------------------
    // Save as ncorr native binary
    // -----------------------------------------------------------------------
    save(output, output_path);
    LOG_INFO << "Saved: " << output_path;

    // -----------------------------------------------------------------------
    // Verification: reload and compare
    // -----------------------------------------------------------------------
    LOG_INFO << "Verifying round-trip...";
    auto reloaded = DIC_analysis_output::load(output_path);
    bool ok = true;
    if (reloaded.disps.size() != output.disps.size()) {
        LOG_ERROR << "FAIL: frame count mismatch: " << reloaded.disps.size()
                  << " vs " << output.disps.size();
        ok = false;
    }
    if (reloaded.units != output.units) {
        LOG_ERROR << "FAIL: units mismatch: \"" << reloaded.units
                  << "\" vs \"" << output.units << "\"";
        ok = false;
    }
    if (std::abs(reloaded.units_per_pixel - output.units_per_pixel) > 1e-12) {
        LOG_ERROR << "FAIL: units_per_pixel mismatch: " << reloaded.units_per_pixel
                  << " vs " << output.units_per_pixel;
        ok = false;
    }
    if (ok && !reloaded.disps.empty()) {
        for (size_t f = 0; f < reloaded.disps.size(); ++f) {
            if (reloaded.disps[f].data_height() > 0) {
                LOG_INFO << "  Reloaded frame " << f << " size: "
                         << reloaded.disps[f].data_height() << "x"
                         << reloaded.disps[f].data_width()
                         << ", ROI regions: " << reloaded.disps[f].get_roi().size_regions()
                         << ", scalefactor: " << reloaded.disps[f].get_scalefactor();
                break;
            }
        }
    }

    if (ok) {
        LOG_INFO << "Verification OK";
    } else {
        LOG_ERROR << "Verification FAILED";
        return 1;
    }

    return 0;
}
