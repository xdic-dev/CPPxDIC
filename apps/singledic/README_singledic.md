# singledic — single-camera 2D DIC (STUB)

`singledic` runs 2D Digital Image Correlation on a **single** camera's image sequence and
produces in-plane displacement and strain fields. Unlike the full `xdic` camerapairs
pipeline, it performs **no stereo pairing and no 3D surface reconstruction**.

> **Status:** STUB. The current `main.cpp` parses the MATLAB reference folder path,
> logs `singledic not yet implemented`, and exits cleanly. See the `TODO` block in
> `main.cpp` for the definition of "fully implemented".

## MATLAB behavioural reference

The equivalent step in the reference MATLAB project (`Tools/MultiDIC`) is the 2D-DIC step,
which runs ncorr per camera before any stereo step:

- `Tools/MultiDIC/main_script/stepD_2DDIC.m` — standard per-camera 2D DIC (the single-camera
  behaviour to reproduce).
- `Tools/MultiDIC/main_script/stepD_2DDIC_MNG.m` — mirrored-camera variant (reference for the
  separate `xdic` mirrored mode, not for singledic).

Match `singledic`'s displacement/strain outputs against the per-camera DIC results produced
by `stepD_2DDIC.m` on the same input sequence.

## Building

```bash
cmake -DBUILD_SINGLEDIC=ON -B build -S .
cmake --build build --target singledic
```

The target is **OFF by default** and is not built as part of the default `cppxdic` build.

## Usage (stub)

```bash
./singledic --matlab-ref /path/to/Tools/MultiDIC/main_script
```
