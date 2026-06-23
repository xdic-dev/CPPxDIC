"""
Load CPPxDIC result artifacts into plain JSON-able structures for the plot UI.

``.mat`` files written by matio may be either classic (<= v7, read by
``scipy.io.loadmat``) or v7.3/HDF5 (read by ``h5py``). ``load_mat`` tries scipy
first and falls back to h5py, returning a flat ``{name: ndarray}`` dict.

Targets the GUI plots:
  * ncorr<N>.mat / .csv : per-frame U/V displacement grids + correlation C
  * DIC3D*Pairs_*.mat / DIC3DPPresults_*.mat : 3D points/faces + strain measures
"""

from __future__ import annotations

import csv
import os
from typing import Any

import numpy as np


# ---- generic .mat loading ---------------------------------------------------

def load_mat(path: str) -> dict[str, Any]:
    """Load a .mat file (v7 or v7.3) into a {name: value} dict.

    Tries scipy.io.loadmat first (classic format); on failure (NotImplementedError
    is raised by scipy for v7.3) falls back to h5py.
    """
    try:
        from scipy.io import loadmat

        data = loadmat(path, squeeze_me=True, struct_as_record=False)
        return {k: v for k, v in data.items() if not k.startswith("__")}
    except NotImplementedError:
        return _load_mat_v73(path)
    except Exception:
        # Some valid v7.3 files also raise ValueError from scipy — try h5py.
        return _load_mat_v73(path)


def _load_mat_v73(path: str) -> dict[str, Any]:
    import h5py

    out: dict[str, Any] = {}
    with h5py.File(path, "r") as f:
        for key in f.keys():
            try:
                arr = np.array(f[key])
                # MATLAB/HDF5 stores arrays transposed relative to numpy.
                if arr.ndim >= 2:
                    arr = arr.T
                out[key] = arr
            except Exception:
                out[key] = None
    return out


# ---- ncorr (2D displacement) ------------------------------------------------

def _coerce_2d_stack(value: Any) -> list[np.ndarray]:
    """Coerce a value that may hold per-frame 2D grids into a list of 2D arrays."""
    if value is None:
        return []
    arr = np.asarray(value, dtype=object) if isinstance(value, (list, tuple)) else value
    # Cell-array-like: object array of 2D grids.
    if isinstance(arr, np.ndarray) and arr.dtype == object:
        return [np.asarray(a, dtype=float) for a in arr.ravel() if np.ndim(a) == 2]
    arr = np.asarray(value, dtype=float)
    if arr.ndim == 2:
        return [arr]
    if arr.ndim == 3:
        # Assume frame axis is the longest-leading or trailing; pick smallest axis as frames.
        frame_axis = int(np.argmin(arr.shape))
        return [np.take(arr, i, axis=frame_axis) for i in range(arr.shape[frame_axis])]
    return []


def load_ncorr_fields(path: str) -> dict[str, list[np.ndarray]]:
    """Return {'U': [...], 'V': [...], 'C': [...]} per-frame grids from an ncorr file."""
    if path.lower().endswith(".csv"):
        return _load_ncorr_csv(path)
    data = load_mat(path)
    fields: dict[str, list[np.ndarray]] = {}
    # Common key spellings across the pipeline / matio exports.
    aliases = {
        "U": ["U", "U_k", "u", "Uk"],
        "V": ["V", "V_k", "v", "Vk"],
        "C": ["C", "C_k", "corrcoef", "Ck", "CorCoeffVec"],
    }
    for canonical, names in aliases.items():
        for n in names:
            if n in data and data[n] is not None:
                stack = _coerce_2d_stack(data[n])
                if stack:
                    fields[canonical] = stack
                    break
    return fields


def _load_ncorr_csv(path: str) -> dict[str, list[np.ndarray]]:
    """Best-effort CSV reader: columns u,v,corrcoef (one row per grid point)."""
    rows: list[dict[str, str]] = []
    with open(path, newline="") as fh:
        reader = csv.DictReader(fh)
        for r in reader:
            rows.append(r)
    if not rows:
        return {}

    def col(*names):
        for n in names:
            if n in rows[0]:
                return [float(r[n]) for r in rows]
        return None

    u, v, c = col("u", "U"), col("v", "V"), col("corrcoef", "C", "c")
    out: dict[str, list[np.ndarray]] = {}
    for key, vals in (("U", u), ("V", v), ("C", c)):
        if vals is not None:
            out[key] = [np.asarray(vals, dtype=float).reshape(-1, 1)]
    return out


# ---- 3D results -------------------------------------------------------------

def load_dic3d(path: str) -> dict[str, Any]:
    """Extract points / faces / per-frame scalar fields from a DIC3D* file.

    Returns a structure the frontend can render with Plotly mesh3d/scatter3d.
    Best-effort: shapes vary across pipeline versions, so missing pieces are
    simply omitted.
    """
    data = load_mat(path)
    result: dict[str, Any] = {"keys": sorted(data.keys())}

    points = _first(data, ["PointsGlobal", "Points", "FaceCentroids"])
    if points is not None:
        pts = np.asarray(points, dtype=float)
        if pts.ndim == 2 and pts.shape[1] == 3:
            result["points"] = pts.tolist()
        elif pts.ndim == 2 and pts.shape[0] == 3:
            result["points"] = pts.T.tolist()

    faces = _first(data, ["Faces", "F"])
    if faces is not None:
        f = np.asarray(faces, dtype=float)
        if f.ndim == 2 and 3 in f.shape:
            if f.shape[1] != 3:
                f = f.T
            # MATLAB faces are 1-based.
            result["faces"] = (f.astype(int) - 1).tolist()

    # Surface scalar fields available to color the mesh (e.g. Epc1, Epc2).
    scalar_candidates = ["Epc1", "Epc2", "DispMgn", "DisplacementMgn", "FaceIsoInd",
                          "FaceCorrComb", "FaceColors"]
    result["scalar_fields"] = [k for k in scalar_candidates if k in data and data[k] is not None]
    return result


def dic3d_scalar(path: str, field: str) -> list[float]:
    data = load_mat(path)
    if field not in data or data[field] is None:
        return []
    arr = np.asarray(data[field], dtype=float)
    return arr.ravel().tolist()


def _first(data: dict, names: list[str]):
    for n in names:
        if n in data and data[n] is not None:
            return data[n]
    return None


# ---- discovery --------------------------------------------------------------

def list_result_files(root: str) -> list[str]:
    """Find ncorr*.mat/.csv and DIC3D*.mat files under root (relative paths)."""
    hits: list[str] = []
    if not root or not os.path.isdir(root):
        return hits
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            low = name.lower()
            if (low.startswith("ncorr") and (low.endswith(".mat") or low.endswith(".csv"))) \
                    or (low.startswith("dic3d") and low.endswith(".mat")):
                hits.append(os.path.relpath(os.path.join(dirpath, name), root))
    return sorted(hits)
