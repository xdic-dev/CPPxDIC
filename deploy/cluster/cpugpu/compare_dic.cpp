// compare_dic — semantic comparison of one variable across two .mat files
// (MATLAB v5 or v7.3; matio reads both). Recurses structs/cells, compares
// numeric/logical arrays elementwise with NaN==NaN, and reports per-leaf:
//   n elements, n differing (exact), max |diff|, counts above delta thresholds,
//   NaN-pattern mismatches (valid-point-set differences).
//
// Usage: compare_dic <fileA> <fileB> <varname>
// Build: g++ -O2 -o compare_dic compare_dic.cpp -lmatio
#include <matio.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const double THRESH[] = {1e-12, 1e-9, 1e-6, 1e-3, 1e-1};
static const int NT = 5;

struct Totals {
    long long fields = 0, identical = 0, differing = 0, mismatched = 0;
    long long elems = 0, diff_exact = 0, nan_mismatch = 0;
    long long above[NT] = {0, 0, 0, 0, 0};
    double max_abs = 0.0;
} G;

static size_t numel(const matvar_t* v) {
    size_t n = 1;
    for (int i = 0; i < v->rank; ++i) n *= v->dims[i];
    return n;
}

static std::string dimstr(const matvar_t* v) {
    std::string s = "[";
    for (int i = 0; i < v->rank; ++i)
        s += std::to_string(v->dims[i]) + (i + 1 < v->rank ? "x" : "");
    return s + "]";
}

// Fetch element as double (handles the numeric/logical classes we meet here).
static bool elem(const matvar_t* v, size_t i, double& out) {
    const void* d = v->data;
    if (!d) return false;
    switch (v->class_type) {
        case MAT_C_DOUBLE: out = ((const double*)d)[i]; return true;
        case MAT_C_SINGLE: out = ((const float*)d)[i]; return true;
        case MAT_C_INT8:   out = ((const int8_t*)d)[i]; return true;
        case MAT_C_UINT8:  out = ((const uint8_t*)d)[i]; return true;
        case MAT_C_INT16:  out = ((const int16_t*)d)[i]; return true;
        case MAT_C_UINT16: out = ((const uint16_t*)d)[i]; return true;
        case MAT_C_INT32:  out = ((const int32_t*)d)[i]; return true;
        case MAT_C_UINT32: out = ((const uint32_t*)d)[i]; return true;
        case MAT_C_INT64:  out = (double)((const int64_t*)d)[i]; return true;
        case MAT_C_UINT64: out = (double)((const uint64_t*)d)[i]; return true;
        case MAT_C_CHAR:   out = ((const uint16_t*)d)[i]; return true;  // mat5 chars are u16
        default: return false;
    }
}

static bool isNumericLike(const matvar_t* v) {
    switch (v->class_type) {
        case MAT_C_DOUBLE: case MAT_C_SINGLE:
        case MAT_C_INT8: case MAT_C_UINT8: case MAT_C_INT16: case MAT_C_UINT16:
        case MAT_C_INT32: case MAT_C_UINT32: case MAT_C_INT64: case MAT_C_UINT64:
            return true;
        default: return false;
    }
}

static void leafNumeric(const std::string& path, const matvar_t* a, const matvar_t* b) {
    // dims must match up to vector transposition ({1,N} vs {N,1})
    size_t na = numel(a), nb = numel(b);
    bool dimok = (na == nb);
    if (dimok && a->rank == b->rank) {
        bool same = true, transposed_vec = false;
        for (int i = 0; i < a->rank; ++i) same &= (a->dims[i] == b->dims[i]);
        if (!same && a->rank == 2 &&
            a->dims[0] == b->dims[1] && a->dims[1] == b->dims[0] &&
            (a->dims[0] == 1 || a->dims[1] == 1))
            transposed_vec = true;  // linear order identical for vectors
        if (!same && !transposed_vec) {
            printf("SHAPE     %-70s %s vs %s\n", path.c_str(), dimstr(a).c_str(), dimstr(b).c_str());
            G.mismatched++;
            return;
        }
    }
    if (!dimok) {
        printf("SHAPE     %-70s %s vs %s\n", path.c_str(), dimstr(a).c_str(), dimstr(b).c_str());
        G.mismatched++;
        return;
    }
    long long nd = 0, nnan = 0, above[NT] = {0, 0, 0, 0, 0};
    double mx = 0.0;
    for (size_t i = 0; i < na; ++i) {
        double x, y;
        if (!elem(a, i, x) || !elem(b, i, y)) continue;
        bool xn = std::isnan(x), yn = std::isnan(y);
        if (xn && yn) continue;
        if (xn != yn) { nnan++; nd++; continue; }
        if (x != y) {
            nd++;
            double d = std::fabs(x - y);
            if (d > mx) mx = d;
            for (int t = 0; t < NT; ++t)
                if (d > THRESH[t]) above[t]++;
        }
    }
    G.fields++;
    G.elems += (long long)na;
    G.diff_exact += nd;
    G.nan_mismatch += nnan;
    for (int t = 0; t < NT; ++t) G.above[t] += above[t];
    if (mx > G.max_abs) G.max_abs = mx;
    if (nd == 0) {
        G.identical++;
        printf("IDENTICAL %-70s n=%zu\n", path.c_str(), na);
    } else {
        G.differing++;
        printf("DIFFERS   %-70s n=%zu diff=%lld nanMis=%lld max=%.3e  >1e-12:%lld >1e-9:%lld >1e-6:%lld >1e-3:%lld >1e-1:%lld\n",
               path.c_str(), na, nd, nnan, mx, above[0], above[1], above[2], above[3], above[4]);
    }
}

static void walk(const std::string& path, matvar_t* a, matvar_t* b) {
    if (!a || !b) {
        printf("MISSING   %-70s %s\n", path.c_str(), !a ? "in A" : "in B");
        G.mismatched++;
        return;
    }
    if (a->class_type == MAT_C_STRUCT && b->class_type == MAT_C_STRUCT) {
        size_t sa = numel(a), sb = numel(b);
        // MATLAB pipelines store per-frame data as a struct ARRAY
        // (displacements(f).plot_u_dic) while cppxdic writes a scalar struct
        // of cells (displacements.plot_u_dic{f}). Adapt: compare A(i).field
        // against B.field{i} (and the mirrored case).
        if (sa != sb && (sa == 1 || sb == 1)) {
            bool aIsArray = sa > 1;
            matvar_t* arr = aIsArray ? a : b;   // struct array side
            matvar_t* sc = aIsArray ? b : a;    // scalar struct-of-cells side
            size_t n = numel(arr);
            unsigned nf = Mat_VarGetNumberOfFields(arr);
            char* const* names = Mat_VarGetStructFieldnames(arr);
            for (unsigned i = 0; i < nf; ++i) {
                matvar_t* cellf = Mat_VarGetStructFieldByName(sc, names[i], 0);
                std::string p = path + "." + names[i];
                if (!cellf) { printf("MISSING   %-70s in %s\n", p.c_str(), aIsArray ? "B" : "A"); G.mismatched++; continue; }
                if (cellf->class_type != MAT_C_CELL || numel(cellf) != n) {
                    printf("SHAPE     %-70s struct-array[%zu] vs %s\n", p.c_str(), n,
                           cellf->class_type == MAT_C_CELL ? ("cell" + dimstr(cellf)).c_str() : "non-cell");
                    G.mismatched++;
                    continue;
                }
                for (size_t k = 0; k < n; ++k) {
                    matvar_t* ea = Mat_VarGetStructFieldByName(arr, names[i], k);
                    matvar_t* eb = Mat_VarGetCell(cellf, (int)k);
                    if ((!ea || numel(ea) == 0) && (!eb || numel(eb) == 0)) continue;
                    std::string pk = p + "{" + std::to_string(k + 1) + "}";
                    if (aIsArray) walk(pk, ea, eb); else walk(pk, eb, ea);
                }
            }
            return;
        }
        if (sa != sb) {
            printf("SHAPE     %-70s struct%s vs struct%s\n", path.c_str(), dimstr(a).c_str(), dimstr(b).c_str());
            G.mismatched++;
            return;
        }
        unsigned nf = Mat_VarGetNumberOfFields(a);
        char* const* names = Mat_VarGetStructFieldnames(a);
        for (unsigned i = 0; i < nf; ++i) {
            for (size_t k = 0; k < sa; ++k) {
                matvar_t* fa = Mat_VarGetStructFieldByName(a, names[i], k);
                matvar_t* fb = Mat_VarGetStructFieldByName(b, names[i], k);
                std::string p = path + (sa > 1 ? "(" + std::to_string(k + 1) + ")" : "") + "." + names[i];
                if (!fb && !fa) continue;
                if (!fb) { printf("MISSING   %-70s in B\n", p.c_str()); G.mismatched++; continue; }
                if (!fa) { printf("MISSING   %-70s in A\n", p.c_str()); G.mismatched++; continue; }
                walk(p, fa, fb);
            }
        }
        // report B-only fields
        unsigned nfb = Mat_VarGetNumberOfFields(b);
        char* const* namesb = Mat_VarGetStructFieldnames(b);
        for (unsigned i = 0; i < nfb; ++i)
            if (!Mat_VarGetStructFieldByName(a, namesb[i], 0)) {
                printf("MISSING   %-70s in A\n", (path + "." + namesb[i]).c_str());
                G.mismatched++;
            }
        return;
    }
    if (a->class_type == MAT_C_CELL && b->class_type == MAT_C_CELL) {
        size_t na = numel(a), nb = numel(b);
        if (na != nb) {
            printf("SHAPE     %-70s cell %s vs %s\n", path.c_str(), dimstr(a).c_str(), dimstr(b).c_str());
            G.mismatched++;
            return;
        }
        for (size_t i = 0; i < na; ++i) {
            matvar_t* ca = Mat_VarGetCell(a, (int)i);
            matvar_t* cb = Mat_VarGetCell(b, (int)i);
            if ((!ca || numel(ca) == 0) && (!cb || numel(cb) == 0)) continue;  // both empty
            walk(path + "{" + std::to_string(i + 1) + "}", ca, cb);
        }
        return;
    }
    if (isNumericLike(a) && isNumericLike(b)) {
        leafNumeric(path, a, b);
        return;
    }
    if (a->class_type == MAT_C_CHAR && b->class_type == MAT_C_CHAR) {
        // strings: informational only (paths etc. legitimately differ)
        printf("STRING    %-70s (skipped)\n", path.c_str());
        return;
    }
    printf("CLASSMIS  %-70s class %d vs %d\n", path.c_str(), (int)a->class_type, (int)b->class_type);
    G.mismatched++;
}

// Read "name" or dotted "name.field.sub" (struct descent after the first
// token). A token may end in "{K}" (1-based) to index into a cell array.
static matvar_t* resolveToken(matvar_t* v, std::string tok) {
    long long cellIdx = -1;
    size_t brace = tok.find('{');
    if (brace != std::string::npos) {
        cellIdx = std::stoll(tok.substr(brace + 1));
        tok = tok.substr(0, brace);
    }
    if (!tok.empty()) {
        if (!v || v->class_type != MAT_C_STRUCT) return nullptr;
        v = Mat_VarGetStructFieldByName(v, tok.c_str(), 0);
    }
    if (cellIdx > 0) {
        if (!v || v->class_type != MAT_C_CELL) return nullptr;
        v = Mat_VarGetCell(v, (int)(cellIdx - 1));
    }
    return v;
}

// HDF5 (v7.3) reads can defer nested cell/field data; force-load recursively
// (same remedy as MatReader::readDIC3Dcombined's deep_load).
static void deepLoad(mat_t* f, matvar_t* v) {
    if (!v) return;
    Mat_VarReadDataAll(f, v);
    size_t ne = 1;
    for (int i = 0; i < v->rank; ++i) ne *= v->dims[i];
    if (v->class_type == MAT_C_STRUCT) {
        unsigned nf = Mat_VarGetNumberOfFields(v);
        char* const* names = Mat_VarGetStructFieldnames(v);
        for (size_t e = 0; e < ne; ++e)
            for (unsigned i = 0; i < nf; ++i)
                deepLoad(f, Mat_VarGetStructFieldByName(v, names[i], e));
    } else if (v->class_type == MAT_C_CELL) {
        for (size_t e = 0; e < ne; ++e) deepLoad(f, Mat_VarGetCell(v, (int)e));
    }
}

static matvar_t* readPath(mat_t* f, const std::string& spec) {
    size_t dot = spec.find('.');
    std::string head = spec.substr(0, dot);
    // top-level: read variable by name (strip {K} first, apply after)
    std::string headName = head.substr(0, head.find('{'));
    matvar_t* v = Mat_VarRead(f, headName.c_str());
    deepLoad(f, v);
    if (head.find('{') != std::string::npos)
        v = resolveToken(v, head.substr(head.find('{')));
    while (v && dot != std::string::npos) {
        size_t next = spec.find('.', dot + 1);
        std::string field = spec.substr(dot + 1, next == std::string::npos
                                                     ? std::string::npos
                                                     : next - dot - 1);
        v = resolveToken(v, field);
        dot = next;
    }
    return v;
}

static void listVars(const char* fname) {
    mat_t* f = Mat_Open(fname, MAT_ACC_RDONLY);
    if (!f) { fprintf(stderr, "cannot open %s\n", fname); return; }
    matvar_t* v;
    while ((v = Mat_VarReadNextInfo(f)) != nullptr) {
        printf("%s: %s class=%d rank=%d\n", fname, v->name, (int)v->class_type, v->rank);
        Mat_VarFree(v);
    }
    Mat_Close(f);
}

static void dumpVar(const char* fname, const std::string& path) {
    mat_t* f = Mat_Open(fname, MAT_ACC_RDONLY);
    if (!f) { fprintf(stderr, "cannot open %s\n", fname); return; }
    matvar_t* v = readPath(f, path);
    if (!v) { printf("DUMP %s: <unreadable>\n", path.c_str()); return; }
    printf("DUMP %s: class=%d type=%d dims=%s data=%p\n", path.c_str(),
           (int)v->class_type, (int)v->data_type, dimstr(v).c_str(), v->data);
    if (v->class_type == MAT_C_CELL) {
        size_t n = numel(v);
        for (size_t i = 0; i < n && i < 3; ++i) {
            matvar_t* c = Mat_VarGetCell(v, (int)i);
            if (!c) { printf("  cell{%zu}: NULL\n", i + 1); continue; }
            printf("  cell{%zu}: class=%d type=%d dims=%s data=%p\n", i + 1,
                   (int)c->class_type, (int)c->data_type, dimstr(c).c_str(), c->data);
            if (c->class_type == MAT_C_DOUBLE && c->data && c->rank == 2) {
                size_t R = c->dims[0];
                for (size_t r = 0; r < R && r < 4; ++r) {
                    printf("    row%zu:", r + 1);
                    for (size_t cc = 0; cc < (size_t)c->dims[1] && cc < 4; ++cc)
                        printf(" %.6f", ((const double*)c->data)[r + cc * R]);
                    printf("\n");
                }
            }
        }
    }
    if (v->class_type == MAT_C_DOUBLE && v->data && v->rank == 2) {
        size_t R = v->dims[0];
        for (size_t r = 0; r < R && r < 4; ++r) {
            printf("  row%zu:", r + 1);
            for (size_t cc = 0; cc < (size_t)v->dims[1] && cc < 4; ++cc)
                printf(" %.6f", ((const double*)v->data)[r + cc * R]);
            printf("\n");
        }
        size_t n = numel(v), nn = 0, nz = 0;
        double mn = 1e300, mx = -1e300, sum = 0;
        for (size_t i = 0; i < n; ++i) {
            double x = ((const double*)v->data)[i];
            if (std::isnan(x)) { nn++; continue; }
            if (x == 0.0) nz++;
            if (x < mn) mn = x;
            if (x > mx) mx = x;
            sum += x;
        }
        printf("  nan: %zu / %zu  zeros: %zu  min/mean/max: %.4g / %.4g / %.4g\n",
               nn, n, nz, mn, n > nn ? sum / (n - nn) : 0.0, mx);
    }
    if (v->class_type == MAT_C_STRUCT) {
        unsigned nf = Mat_VarGetNumberOfFields(v);
        char* const* names = Mat_VarGetStructFieldnames(v);
        for (unsigned i = 0; i < nf; ++i) {
            matvar_t* c = Mat_VarGetStructFieldByName(v, names[i], 0);
            printf("  .%s: class=%d dims=%s", names[i], c ? (int)c->class_type : -1,
                   c ? dimstr(c).c_str() : "?");
            if (c && c->class_type == MAT_C_DOUBLE && c->data)
                for (size_t k = 0; k < numel(c) && k < 6; ++k)
                    printf(" %g", ((const double*)c->data)[k]);
            if (c && c->class_type == MAT_C_CHAR && c->data) {
                printf(" '");
                for (size_t k = 0; k < numel(c) && k < 24; ++k)
                    printf("%c", (char)((const uint16_t*)c->data)[k]);
                printf("'");
            }
            printf("\n");
        }
    }
    Mat_Close(f);
}

#include <map>

// POINTS mode: point-matched comparison of the final DIC2DpairResults.
// Rows of Points{k} correspond to the same physical point across all 2*nImages
// cells within one file, but the row SETS differ across implementations
// (slightly different valid subsets). Match rows via the frame-1 (cam-ref)
// coordinates — seed points lie on the exact analysis grid, so keys are exact —
// then compare matched trajectories and correlation coefficients.
static int comparePoints(const char* fA, const char* fB, const char* varA, const char* varB) {
    mat_t* fa = Mat_Open(fA, MAT_ACC_RDONLY);
    mat_t* fb = Mat_Open(fB, MAT_ACC_RDONLY);
    if (!fa || !fb) { fprintf(stderr, "open failed\n"); return 2; }
    matvar_t* PA = readPath(fa, std::string(varA) + "Points");
    matvar_t* CA = readPath(fa, std::string(varA) + "CorCoeffVec");
    matvar_t* PB = readPath(fb, std::string(varB) + "Points");
    matvar_t* CB = readPath(fb, std::string(varB) + "CorCoeffVec");
    if (!PA || !PB || !CA || !CB) { fprintf(stderr, "Points/CorCoeffVec missing\n"); return 2; }
    size_t nc = numel(PA);
    if (numel(PB) != nc) { printf("POINTS: cell counts differ %zu vs %zu\n", numel(PA), numel(PB)); return 1; }

    auto rows = [](matvar_t* m) { return m->dims[0]; };
    auto val = [](matvar_t* m, size_t r, size_t c) { return ((const double*)m->data)[r + c * m->dims[0]]; };

    matvar_t* a1 = Mat_VarGetCell(PA, 0);
    matvar_t* b1 = Mat_VarGetCell(PB, 0);
    matvar_t* a2 = Mat_VarGetCell(PA, 1);
    matvar_t* b2 = Mat_VarGetCell(PB, 1);
    // Frame-1 coords are exact grid nodes -> exact keys. Fallback: cuNCorr's
    // pair results leave frame 1 all-NaN, so when either side's frame 1 is
    // mostly NaN, key BOTH sides on frame-2 coords bucketed at 0.5 px (grid
    // spacing ~11 px and cross-side frame-2 agreement ~0.01 px, so buckets are
    // unambiguous; boundary stragglers just drop out of the matched set).
    auto nanFrac = [&](matvar_t* m) {
        size_t n = 0;
        for (size_t r = 0; r < rows(m); ++r) n += std::isnan(val(m, r, 0));
        return (double)n / (double)rows(m);
    };
    bool fallback = nanFrac(a1) > 0.5 || nanFrac(b1) > 0.5;
    matvar_t* ka = fallback ? a2 : a1;
    matvar_t* kb = fallback ? b2 : b1;
    double scale = fallback ? 2.0 : 1e6;  // 0.5 px buckets vs exact
    if (fallback) printf("POINTS: frame-1 grid unavailable (NaN) -> matching on frame-2 coords, 0.5 px buckets\n");
    std::map<std::pair<long long, long long>, size_t> keyB;
    auto key = [&](matvar_t* m, size_t r) {
        return std::make_pair((long long)llround(val(m, r, 0) * scale),
                              (long long)llround(val(m, r, 1) * scale));
    };
    for (size_t r = 0; r < rows(kb); ++r)
        if (!std::isnan(val(kb, r, 0))) keyB[key(kb, r)] = r;
    std::vector<std::pair<size_t, size_t>> match;  // rowA -> rowB
    size_t aOnly = 0;
    for (size_t r = 0; r < rows(ka); ++r) {
        if (std::isnan(val(ka, r, 0))) { aOnly++; continue; }
        auto it = keyB.find(key(ka, r));
        if (it == keyB.end()) { aOnly++; continue; }
        match.push_back({r, it->second});
    }
    printf("POINTS: A=%zu B=%zu matched=%zu A-only/unkeyed=%zu B-only=%zu\n",
           (size_t)rows(a1), (size_t)rows(b1), match.size(), aOnly, rows(b1) - match.size());

    double mxP = 0, mxC = 0;
    long long nP = 0, aboveP[NT] = {0}, nC = 0, aboveC[NT] = {0}, nanMis = 0;
    size_t argFrame = 0, argRow = 0;
    for (size_t k = 0; k < nc; ++k) {
        matvar_t* pa = Mat_VarGetCell(PA, (int)k);
        matvar_t* pb = Mat_VarGetCell(PB, (int)k);
        matvar_t* ca = Mat_VarGetCell(CA, (int)k);
        matvar_t* cb = Mat_VarGetCell(CB, (int)k);
        if (!pa || !pb) continue;
        for (auto& mr : match) {
            for (int c = 0; c < 2; ++c) {
                double x = val(pa, mr.first, c), y = val(pb, mr.second, c);
                bool xn = std::isnan(x), yn = std::isnan(y);
                if (xn && yn) continue;
                if (xn != yn) { nanMis++; continue; }
                double d = std::fabs(x - y);
                if (d > 0) {
                    nP++;
                    if (d > mxP) { mxP = d; argFrame = k + 1; argRow = mr.first + 1; }
                    for (int t = 0; t < NT; ++t)
                        if (d > THRESH[t]) aboveP[t]++;
                }
            }
            if (ca && cb && ca->data && cb->data) {
                double x = ((const double*)ca->data)[mr.first];
                double y = ((const double*)cb->data)[mr.second];
                if (!std::isnan(x) && !std::isnan(y) && x != y) {
                    nC++;
                    double d = std::fabs(x - y);
                    if (d > mxC) mxC = d;
                    for (int t = 0; t < NT; ++t)
                        if (d > THRESH[t]) aboveC[t]++;
                }
            }
        }
    }
    long long tot = (long long)match.size() * (long long)nc * 2;
    printf("POINTS coords: compared=%lld diff=%lld nanMis=%lld max=%.6e (cell %zu, row %zu) "
           ">1e-12:%lld >1e-9:%lld >1e-6:%lld >1e-3:%lld >1e-1:%lld\n",
           tot, nP, nanMis, mxP, argFrame, argRow,
           aboveP[0], aboveP[1], aboveP[2], aboveP[3], aboveP[4]);

    // --- Spatial diagnosis: are the big-difference points at the ROI edge? ---
    // A grid point is a BOUNDARY point if any 4-neighbour (grid spacing apart,
    // inferred from the data) is missing from the frame-1 valid set. Uses the
    // A side's frame-1 grid (exact node coordinates); skipped in fallback mode
    // where frame-1 keys were unavailable on one side (keys are then bucketed).
    if (!fallback) {
        // infer grid spacing: smallest positive x-delta among a sample of nodes
        double sp = 1e30;
        for (size_t r = 1; r < rows(a1) && r < 5000; ++r) {
            double d = std::fabs(val(a1, r, 0) - val(a1, r - 1, 0));
            if (d > 1e-9 && d < sp) sp = d;
            d = std::fabs(val(a1, r, 1) - val(a1, r - 1, 1));
            if (d > 1e-9 && d < sp) sp = d;
        }
        std::map<std::pair<long long, long long>, bool> gridA;
        for (size_t r = 0; r < rows(a1); ++r) gridA[key(a1, r)] = true;
        auto isBoundary = [&](size_t r) {
            double x = val(a1, r, 0), y = val(a1, r, 1);
            const double dx[4] = {sp, -sp, 0, 0}, dy[4] = {0, 0, sp, -sp};
            for (int k = 0; k < 4; ++k) {
                auto nb = std::make_pair((long long)llround((x + dx[k]) * 1e6),
                                         (long long)llround((y + dy[k]) * 1e6));
                if (gridA.find(nb) == gridA.end()) return true;
            }
            return false;
        };
        // per-row max |diff| across all cells
        std::vector<double> rowMax(match.size(), 0.0);
        for (size_t k = 0; k < nc; ++k) {
            matvar_t* pa = Mat_VarGetCell(PA, (int)k);
            matvar_t* pb = Mat_VarGetCell(PB, (int)k);
            if (!pa || !pb) continue;
            for (size_t m = 0; m < match.size(); ++m) {
                for (int c = 0; c < 2; ++c) {
                    double x = val(pa, match[m].first, c), y = val(pb, match[m].second, c);
                    if (std::isnan(x) || std::isnan(y)) continue;
                    double d = std::fabs(x - y);
                    if (d > rowMax[m]) rowMax[m] = d;
                }
            }
        }
        // boundary depth 2: within 2 grid cells of a missing node
        auto isBoundary2 = [&](size_t r) {
            double x = val(a1, r, 0), y = val(a1, r, 1);
            for (int ky = -2; ky <= 2; ++ky)
                for (int kx = -2; kx <= 2; ++kx) {
                    if (std::abs(kx) + std::abs(ky) > 2) continue;
                    auto nb = std::make_pair((long long)llround((x + kx * sp) * 1e6),
                                             (long long)llround((y + ky * sp) * 1e6));
                    if (gridA.find(nb) == gridA.end()) return true;
                }
            return false;
        };
        long long nBig = 0, nBigB = 0, nSmall = 0, nSmallB = 0, nAllB = 0;
        long long n01 = 0, n01B2 = 0, nAllB2 = 0;
        double mxInterior = 0;
        for (size_t m = 0; m < match.size(); ++m) {
            bool b = isBoundary(match[m].first);
            bool b2 = isBoundary2(match[m].first);
            if (b) nAllB++;
            if (b2) nAllB2++;
            if (rowMax[m] > 1.0) { nBig++; if (b) nBigB++; }
            else if (rowMax[m] < 0.1) { nSmall++; if (b) nSmallB++; }
            if (rowMax[m] > 0.1) { n01++; if (b2) n01B2++; }
            if (!b2 && rowMax[m] > mxInterior) mxInterior = rowMax[m];
        }
        printf("POINTS spatial (grid spacing %.0f): boundary points overall %lld/%zu (%.0f%%); "
               "rows with maxdiff>1px: %lld, boundary %lld (%.0f%%); rows maxdiff<0.1px: %lld, boundary %lld (%.0f%%)\n",
               sp, nAllB, match.size(), 100.0 * nAllB / match.size(),
               nBig, nBigB, nBig ? 100.0 * nBigB / nBig : 0.0,
               nSmall, nSmallB, nSmall ? 100.0 * nSmallB / nSmall : 0.0);
        printf("POINTS spatial depth2: boundary(<=2 cells) %lld/%zu (%.0f%%); rows maxdiff>0.1px: %lld of which boundary2 %lld (%.0f%%); "
               "max diff among INTERIOR rows: %.3f px\n",
               nAllB2, match.size(), 100.0 * nAllB2 / match.size(),
               n01, n01B2, n01 ? 100.0 * n01B2 / n01 : 0.0, mxInterior);
    }
    printf("POINTS corrcoef: diff=%lld max=%.6e >1e-12:%lld >1e-9:%lld >1e-6:%lld >1e-3:%lld >1e-1:%lld\n",
           nC, mxC, aboveC[0], aboveC[1], aboveC[2], aboveC[3], aboveC[4]);
    return (nP + nC) > 0 ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[2]) == "LIST") { listVars(argv[1]); return 0; }
    if (argc == 4 && std::string(argv[2]) == "DUMP") { dumpVar(argv[1], argv[3]); return 0; }
    if (argc == 6 && std::string(argv[1]) == "POINTS")
        return comparePoints(argv[2], argv[3], argv[4], argv[5]);
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "usage: %s <fileA> <fileB> <varA[.field...]> [varB[.field...]]\n"
                        "       %s <file> LIST\n", argv[0], argv[0]);
        return 2;
    }
    const char* specA = argv[3];
    const char* specB = argc == 5 ? argv[4] : argv[3];
    mat_t* fa = Mat_Open(argv[1], MAT_ACC_RDONLY);
    if (!fa) { fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    mat_t* fb = Mat_Open(argv[2], MAT_ACC_RDONLY);
    if (!fb) { fprintf(stderr, "cannot open %s\n", argv[2]); return 2; }
    matvar_t* va = readPath(fa, specA);
    matvar_t* vb = readPath(fb, specB);
    if (!va || !vb) {
        fprintf(stderr, "variable missing: %s'%s' %s'%s'\n",
                !va ? "A:" : "", specA, !vb ? "B:" : "", specB);
        return 2;
    }
    walk(specA, va, vb);
    printf("== TOTAL leaves=%lld identical=%lld differing=%lld shape/class-mismatch=%lld\n",
           G.fields + G.mismatched, G.identical, G.differing, G.mismatched);
    printf("== TOTAL elems=%lld diff_exact=%lld nan_mismatch=%lld max_abs=%.6e >1e-12:%lld >1e-9:%lld >1e-6:%lld >1e-3:%lld >1e-1:%lld\n",
           G.elems, G.diff_exact, G.nan_mismatch, G.max_abs,
           G.above[0], G.above[1], G.above[2], G.above[3], G.above[4]);
    return G.differing + G.mismatched > 0 ? 1 : 0;
}
