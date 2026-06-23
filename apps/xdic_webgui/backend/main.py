"""
xdic_webgui — FastAPI app serving the CPPxDIC web GUI.

Three feature areas:
  1. Parameter forms  (/api/schema, /api/params)
  2. Media viewer      (/api/media/*)
  3. Interactive plots (/api/results/*)

Static frontend is mounted at "/". Run via ../run.sh (uvicorn backend.main:app).
"""

from __future__ import annotations

import os

from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import Response
from fastapi.staticfiles import StaticFiles

from . import config_schema as cs
from . import media as media_mod
from . import params_io
from . import results as results_mod

app = FastAPI(title="xdic_webgui", version="0.1.0")

_HERE = os.path.dirname(os.path.abspath(__file__))
_FRONTEND_DIR = os.path.normpath(os.path.join(_HERE, "..", "frontend"))


# --- path-safety helper ------------------------------------------------------

def _safe_path(base: str, candidate: str) -> str:
    """Resolve `candidate` (absolute or relative to base) and ensure it stays under base."""
    base = os.path.realpath(base)
    full = candidate if os.path.isabs(candidate) else os.path.join(base, candidate)
    full = os.path.realpath(full)
    if os.path.commonpath([base, full]) != base:
        raise HTTPException(status_code=400, detail="path escapes allowed root")
    return full


def _default_data_path() -> str:
    """Resolve the configured data_path from dic_params.txt (or empty)."""
    try:
        vals = params_io.read_params("dic_params.txt")
        dp = (vals.get("data_path") or "").strip()
        base = (vals.get("base_path") or "").strip()
        if dp:
            return dp
        if base:
            return os.path.join(base, "Data")
    except Exception:
        pass
    return ""


def _default_dic_path() -> str:
    try:
        vals = params_io.read_params("dic_params.txt")
        dp = (vals.get("dic_path") or "").strip()
        base = (vals.get("base_path") or "").strip()
        if dp:
            return dp
        if base:
            return os.path.join(base, "DIC_Output")
    except Exception:
        pass
    return ""


# --- meta --------------------------------------------------------------------

@app.get("/api/config")
def get_config():
    return {
        "repo_root": params_io.repo_root(),
        "target_files": cs.TARGET_FILES,
        "data_path": _default_data_path(),
        "dic_path": _default_dic_path(),
    }


# --- Feature 1: parameter forms ---------------------------------------------

@app.get("/api/schema")
def get_schema(file: str = Query("dic_params.txt")):
    if file not in cs.TARGET_FILES:
        raise HTTPException(status_code=400, detail=f"unknown file: {file}")
    return {"file": file, "groups": cs.groups_for_file(file)}


@app.get("/api/params")
def get_params(file: str = Query("dic_params.txt")):
    if file not in cs.TARGET_FILES:
        raise HTTPException(status_code=400, detail=f"unknown file: {file}")
    return {"file": file, "values": params_io.read_params(file)}


@app.post("/api/params")
def post_params(payload: dict, file: str = Query("dic_params.txt")):
    if file not in cs.TARGET_FILES:
        raise HTTPException(status_code=400, detail=f"unknown file: {file}")
    values = payload.get("values", payload)
    if not isinstance(values, dict):
        raise HTTPException(status_code=400, detail="expected {'values': {...}}")
    path = params_io.write_params(file, values)
    return {"file": file, "written": path, "values": params_io.read_params(file)}


# --- Feature 2: media viewer -------------------------------------------------

@app.get("/api/media/videos")
def media_videos(data_path: str = Query("")):
    root = data_path or _default_data_path()
    return {"data_path": root, "videos": media_mod.list_videos(root)}


@app.get("/api/media/info")
def media_info(path: str, data_path: str = Query("")):
    root = data_path or _default_data_path()
    full = _safe_path(root, path) if root else path
    return media_mod.video_info(full)


def _png_response(png: bytes) -> Response:
    return Response(content=png, media_type="image/png")


@app.get("/api/media/frame")
def media_frame(path: str, index: int = 0, filter: bool = False,
                limit_grayscale: int = 70, data_path: str = Query("")):
    root = data_path or _default_data_path()
    full = _safe_path(root, path) if root else path
    frame = media_mod.read_frame(full, index)
    if filter:
        frame = media_mod.filter_preview(frame, limit_grayscale=limit_grayscale)
    return _png_response(media_mod.to_png(frame))


@app.get("/api/media/overlay")
def media_overlay(path: str, index: int = 0, mask: str = Query(""), seed: str = Query(""),
                  data_path: str = Query("")):
    root = data_path or _default_data_path()
    frame = media_mod.read_frame(_safe_path(root, path) if root else path, index)
    mask_arr = None
    if mask:
        mask_arr = media_mod.load_mask(_safe_path(root, mask) if root else mask,
                                       size=frame.shape[:2])
    seeds = media_mod.load_seeds(_safe_path(root, seed) if root else seed) if seed else None
    return _png_response(media_mod.to_png(media_mod.overlay(frame, mask_arr, seeds)))


# --- Feature 3: interactive plots -------------------------------------------

@app.get("/api/results/list")
def results_list(root: str = Query("")):
    base = root or _default_dic_path()
    return {"root": base, "files": results_mod.list_result_files(base)}


@app.get("/api/results/ncorr")
def results_ncorr(path: str, root: str = Query("")):
    base = root or _default_dic_path()
    full = _safe_path(base, path) if base else path
    fields = results_mod.load_ncorr_fields(full)
    return {
        "available": sorted(fields.keys()),
        "frames": {k: len(v) for k, v in fields.items()},
    }


@app.get("/api/results/ncorr_field")
def results_ncorr_field(path: str, field: str = "U", frame: int = 0, root: str = Query("")):
    base = root or _default_dic_path()
    full = _safe_path(base, path) if base else path
    fields = results_mod.load_ncorr_fields(full)
    if field not in fields:
        raise HTTPException(status_code=404, detail=f"field {field} not in {sorted(fields)}")
    stack = fields[field]
    if not (0 <= frame < len(stack)):
        raise HTTPException(status_code=404, detail=f"frame {frame} out of range (0..{len(stack)-1})")
    grid = stack[frame]
    return {"field": field, "frame": frame, "nframes": len(stack), "z": grid.tolist()}


@app.get("/api/results/dic3d")
def results_dic3d(path: str, root: str = Query("")):
    base = root or _default_dic_path()
    full = _safe_path(base, path) if base else path
    return results_mod.load_dic3d(full)


@app.get("/api/results/dic3d_scalar")
def results_dic3d_scalar(path: str, field: str, root: str = Query("")):
    base = root or _default_dic_path()
    full = _safe_path(base, path) if base else path
    return {"field": field, "values": results_mod.dic3d_scalar(full, field)}


# --- static frontend (mounted last so /api/* wins) ---------------------------

app.mount("/", StaticFiles(directory=_FRONTEND_DIR, html=True), name="frontend")
