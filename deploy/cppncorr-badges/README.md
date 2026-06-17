# CppNCorr infrastructure drop-in

These files are ready-to-apply copies of the GitHub project-quality infrastructure
for the **CppNCorr** repository (`https://github.com/xdic-dev/CppNCorr.git`,
owner/repo = `xdic-dev/CppNCorr`).

You cannot push to CppNCorr from the CPPxDIC worktree, so copy these into a clone
of CppNCorr and commit them there.

## Files in this directory

| File here                          | Copy to (in CppNCorr repo)                |
|------------------------------------|-------------------------------------------|
| `badges-snippet.md`                | paste the badge block at top of `README.md` |
| `Doxyfile`                         | `Doxyfile`                                 |
| `codecov.yml`                      | `codecov.yml`                              |
| `workflows/ci.yml`                 | `.github/workflows/ci.yml`                 |
| `workflows/docs.yml`               | `.github/workflows/docs.yml`               |
| `workflows/release.yml`            | `.github/workflows/release.yml`            |
| `release.yml`                      | `.github/release.yml`                      |

## After copying

1. Verify the dependency list in `workflows/ci.yml` matches what CppNCorr actually
   needs (it ships its own `CMakeLists.txt`). The defaults install OpenCV, FFTW,
   SuiteSparse, BLAS/LAPACK and OpenMP, which the ncorr engine requires.
2. Adjust the `INPUT` paths in `Doxyfile` if CppNCorr's headers/sources are not
   under `include/` and `src/`.
3. Follow the same manual GitHub setup as for CPPxDIC — see
   `docs/PROJECT_INFRA_SETUP.md` in the CPPxDIC repo (Codecov token, Pages source,
   release tags). Everything is identical except the owner/repo is `xdic-dev/CppNCorr`.
