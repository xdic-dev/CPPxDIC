"""
Media helpers: list videos, extract frames, overlay MASK + seed, filter preview.

Reads the same artifacts the C++ pipeline / xdic_stepsABC produce:
  * videos: .mp4 found under the configured data_path
  * MASK:   REF_MASK_*.mat ('refmask' uint8) | raster *_mask.png | .poly polygon
  * seed:   REF_SEED_*.mat ('seed_point') | .seed text (x y per line)

The filter preview approximates the grayscale-limit / percentile-clamp behavior of
``src/image_processor.cpp`` so the user sees roughly what the DIC engine ingests.
It is intentionally a thin numpy reimplementation (documented in the README) and
can be tightened to match the C++ exactly later.
"""

from __future__ import annotations

import os

import cv2
import numpy as np

from .results import load_mat

VIDEO_EXTS = (".mp4", ".avi", ".mov", ".mkv")


# ---- discovery --------------------------------------------------------------

def list_videos(data_path: str) -> list[str]:
    """Return video files under data_path as paths relative to data_path."""
    hits: list[str] = []
    if not data_path or not os.path.isdir(data_path):
        return hits
    for dirpath, _dirs, files in os.walk(data_path):
        for name in files:
            if name.lower().endswith(VIDEO_EXTS):
                hits.append(os.path.relpath(os.path.join(dirpath, name), data_path))
    return sorted(hits)


def video_info(path: str) -> dict:
    cap = cv2.VideoCapture(path)
    if not cap.isOpened():
        raise FileNotFoundError(f"cannot open video: {path}")
    info = {
        "frames": int(cap.get(cv2.CAP_PROP_FRAME_COUNT)),
        "fps": float(cap.get(cv2.CAP_PROP_FPS)),
        "width": int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)),
        "height": int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT)),
    }
    cap.release()
    return info


# ---- frame extraction -------------------------------------------------------

def read_frame(path: str, index: int) -> np.ndarray:
    """Return frame `index` from a video as a BGR uint8 ndarray."""
    cap = cv2.VideoCapture(path)
    if not cap.isOpened():
        raise FileNotFoundError(f"cannot open video: {path}")
    cap.set(cv2.CAP_PROP_POS_FRAMES, max(0, index))
    ok, frame = cap.read()
    cap.release()
    if not ok or frame is None:
        raise ValueError(f"cannot read frame {index} from {path}")
    return frame


def read_image(path: str) -> np.ndarray:
    img = cv2.imread(path, cv2.IMREAD_COLOR)
    if img is None:
        raise FileNotFoundError(f"cannot read image: {path}")
    return img


def to_png(image_bgr: np.ndarray) -> bytes:
    ok, buf = cv2.imencode(".png", image_bgr)
    if not ok:
        raise RuntimeError("PNG encoding failed")
    return buf.tobytes()


# ---- mask / seed loading ----------------------------------------------------

def load_mask(path: str, size: tuple[int, int] | None = None) -> np.ndarray:
    """Load a MASK as a uint8 0/255 single-channel array.

    Accepts a REF_MASK_*.mat ('refmask'), a raster image, or an xdic_stepsABC
    .poly polygon (rasterised against `size` = (height, width)).
    """
    low = path.lower()
    if low.endswith(".mat"):
        data = load_mat(path)
        for key in ("refmask", "ROImask", "mask"):
            if key in data and data[key] is not None:
                m = np.asarray(data[key])
                return ((m > 0).astype(np.uint8)) * 255
        raise ValueError(f"no mask variable found in {path}")
    if low.endswith((".poly", ".txt")):
        if size is None:
            raise ValueError("polygon mask requires a reference image size")
        return _rasterize_polygon(path, size)
    # raster image
    m = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
    if m is None:
        raise FileNotFoundError(f"cannot read mask image: {path}")
    return ((m > 0).astype(np.uint8)) * 255


def _rasterize_polygon(path: str, size: tuple[int, int]) -> np.ndarray:
    pts = []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.replace(",", " ").split()
            if len(parts) >= 2:
                pts.append([float(parts[0]), float(parts[1])])
    mask = np.zeros(size, dtype=np.uint8)
    if len(pts) >= 3:
        cv2.fillPoly(mask, [np.asarray(pts, dtype=np.int32)], 255)
    return mask


def load_seeds(path: str) -> list[tuple[float, float]]:
    """Load seed points as a list of (x, y). Accepts REF_SEED_*.mat or .seed text."""
    low = path.lower()
    if low.endswith(".mat"):
        data = load_mat(path)
        for key in ("seed_point", "seed", "Seed"):
            if key in data and data[key] is not None:
                arr = np.asarray(data[key], dtype=float).reshape(-1)
                if arr.size >= 2:
                    return [(float(arr[0]), float(arr[1]))]
        return []
    seeds: list[tuple[float, float]] = []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.replace(",", " ").split()
            if len(parts) >= 2:
                seeds.append((float(parts[0]), float(parts[1])))
    return seeds


# ---- overlay & filter -------------------------------------------------------

def overlay(frame_bgr: np.ndarray, mask: np.ndarray | None,
            seeds: list[tuple[float, float]] | None) -> np.ndarray:
    out = frame_bgr.copy()
    if mask is not None:
        m = mask
        if m.shape[:2] != out.shape[:2]:
            m = cv2.resize(m, (out.shape[1], out.shape[0]), interpolation=cv2.INTER_NEAREST)
        # Green tint inside ROI + contour outline.
        tint = out.copy()
        tint[m > 0] = (0, 200, 0)
        out = cv2.addWeighted(out, 0.7, tint, 0.3, 0)
        contours, _ = cv2.findContours(m, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        cv2.drawContours(out, contours, -1, (0, 255, 0), 2)
    if seeds:
        for (x, y) in seeds:
            cv2.drawMarker(out, (int(round(x)), int(round(y))), (0, 0, 255),
                           markerType=cv2.MARKER_CROSS, markerSize=20, thickness=2)
            cv2.circle(out, (int(round(x)), int(round(y))), 6, (0, 0, 255), 2)
    return out


def filter_preview(frame_bgr: np.ndarray, limit_grayscale: int = 70,
                   low_pct: float = 1.0, high_pct: float = 99.0) -> np.ndarray:
    """Approximate ImageProcessor: grayscale, clamp low values, percentile-stretch.

    `limit_grayscale` zeroes pixels below the threshold (matches the C++
    grayscale-limit step); the remaining range is percentile-clamped and
    normalised to full 8-bit range.
    """
    gray = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2GRAY).astype(np.float32)
    gray[gray < float(limit_grayscale)] = 0.0
    nonzero = gray[gray > 0]
    if nonzero.size:
        lo = np.percentile(nonzero, low_pct)
        hi = np.percentile(nonzero, high_pct)
        if hi > lo:
            gray = np.clip((gray - lo) / (hi - lo), 0.0, 1.0) * 255.0
    out = gray.astype(np.uint8)
    return cv2.cvtColor(out, cv2.COLOR_GRAY2BGR)
