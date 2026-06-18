"""
Read and write the INI-style CPPxDIC param files.

Mirrors the parse rules of ``Config::parseConfigValue`` / ``parseBool`` /
``parse*List`` in ``src/config.cpp``:
  * ``key = value`` per line; whitespace around key and value is trimmed.
  * Lines starting with ``#`` and blank lines are comments/ignored.
  * Lists are comma-separated (string/int/double).
  * Bools are written as ``true``/``false`` and read as true for {true,1,yes}.

Writing is *in place*: existing comments, section headers and key ordering are
preserved, only the values of known keys are rewritten, and any new keys are
appended. This keeps files human-readable and produces clean diffs, matching the
hand-authored style of the existing ``dic_params.txt``.
"""

from __future__ import annotations

import os
from typing import Any

from . import config_schema as cs

_TRUE_TOKENS = {"true", "1", "yes"}


# ---- value <-> string conversion -------------------------------------------

def _to_python(field: dict, raw: str) -> Any:
    """Convert a raw string value to a typed Python value per the field type."""
    t = field["type"]
    raw = raw.strip()
    if t == cs.BOOL:
        return raw.lower() in _TRUE_TOKENS
    if t == cs.INT:
        try:
            return int(raw)
        except ValueError:
            return field["default"]
    if t == cs.FLOAT:
        try:
            return float(raw)
        except ValueError:
            return field["default"]
    if t == cs.INT_LIST:
        return [int(x) for x in _split_list(raw) if _is_int(x)]
    if t == cs.DOUBLE_LIST:
        return [float(x) for x in _split_list(raw) if _is_float(x)]
    if t == cs.STRING_LIST:
        return _split_list(raw)
    # string / enum
    return raw


def _to_string(field: dict, value: Any) -> str:
    """Convert a typed Python value to its on-disk string form."""
    t = field["type"]
    if t == cs.BOOL:
        return "true" if bool(value) else "false"
    if t in (cs.INT_LIST, cs.DOUBLE_LIST, cs.STRING_LIST):
        if not isinstance(value, (list, tuple)):
            value = [value] if value not in ("", None) else []
        return ",".join(str(x) for x in value)
    if value is None:
        return ""
    return str(value)


def _split_list(raw: str) -> list[str]:
    return [item.strip() for item in raw.split(",") if item.strip() != ""]


def _is_int(s: str) -> bool:
    try:
        int(s)
        return True
    except ValueError:
        return False


def _is_float(s: str) -> bool:
    try:
        float(s)
        return True
    except ValueError:
        return False


# ---- path resolution --------------------------------------------------------

def repo_root() -> str:
    """Repo root used to resolve param/data paths. Overridable via XDIC_REPO_ROOT."""
    env = os.environ.get("XDIC_REPO_ROOT")
    if env:
        return os.path.abspath(env)
    # backend/ -> xdic_webgui/ -> apps/ -> repo root
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(here, "..", "..", ".."))


def resolve_param_path(target_file: str, root: str | None = None) -> str:
    """Resolve a target param file to an absolute path under the repo root.

    Rejects anything outside the schema's known TARGET_FILES to avoid arbitrary
    file writes from the API.
    """
    if target_file not in cs.TARGET_FILES:
        raise ValueError(f"unknown target file: {target_file!r}")
    base = root or repo_root()
    return os.path.normpath(os.path.join(base, target_file))


# ---- read -------------------------------------------------------------------

def read_params(target_file: str, root: str | None = None) -> dict[str, Any]:
    """Return {key: typed value} for every schema field relevant to target_file.

    Keys absent from the file (or with no file present) fall back to schema
    defaults, mirroring the C++ override chain (file overrides compiled default).
    """
    fields = {f["key"]: f for g in cs.groups_for_file(target_file) for f in g["fields"]}
    values: dict[str, Any] = {k: f["default"] for k, f in fields.items()}

    path = resolve_param_path(target_file, root)
    if not os.path.exists(path):
        return values

    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            stripped = line.strip()
            if not stripped or stripped.startswith("#") or "=" not in stripped:
                continue
            key, _, raw = stripped.partition("=")
            key = key.strip()
            if key in fields:
                values[key] = _to_python(fields[key], raw)
    return values


# ---- write ------------------------------------------------------------------

def write_params(target_file: str, values: dict[str, Any], root: str | None = None) -> str:
    """Write values to target_file, preserving existing comments/order in place.

    Only keys present in ``values`` (and known to the schema for this file) are
    written. Existing key lines are updated in place; unknown keys already in the
    file are left untouched; schema keys not yet in the file are appended under a
    trailing section. Returns the absolute path written.
    """
    fields = {f["key"]: f for g in cs.groups_for_file(target_file) for f in g["fields"]}
    # Only accept known keys (silently drop unknowns from the request).
    incoming = {k: v for k, v in values.items() if k in fields}

    path = resolve_param_path(target_file, root)
    os.makedirs(os.path.dirname(path), exist_ok=True)

    existing_lines = []
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as fh:
            existing_lines = fh.readlines()

    written_keys: set[str] = set()
    out_lines: list[str] = []
    for line in existing_lines:
        stripped = line.strip()
        if stripped and not stripped.startswith("#") and "=" in stripped:
            key, _, raw = stripped.partition("=")
            key = key.strip()
            if key in incoming:
                written_keys.add(key)
                # Preserve the original line verbatim when the value is unchanged,
                # so re-saving the full form doesn't reformat untouched fields
                # (e.g. 1e-5 -> 1e-05) and pollute the diff.
                if _to_python(fields[key], raw) != incoming[key]:
                    out_lines.append(f"{key} = {_to_string(fields[key], incoming[key])}\n")
                else:
                    out_lines.append(line if line.endswith("\n") else line + "\n")
                continue
        out_lines.append(line if line.endswith("\n") else line + "\n")

    # Append any schema keys that were supplied but not already present.
    missing = [k for k in incoming if k not in written_keys]
    if missing:
        if out_lines and out_lines[-1].strip() != "":
            out_lines.append("\n")
        out_lines.append("# ---- added by xdic_webgui ----\n")
        for k in missing:
            out_lines.append(f"{k} = {_to_string(fields[k], incoming[k])}\n")

    with open(path, "w", encoding="utf-8") as fh:
        fh.writelines(out_lines)
    return path
