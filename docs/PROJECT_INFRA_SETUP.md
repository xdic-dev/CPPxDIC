# Project Infrastructure Setup — CPPxDIC (and CppNCorr)

This guide walks you through the **manual GitHub steps** that cannot be done from
code, for both repositories:

- **CPPxDIC** — owner/repo `xdic-dev/CPPxDIC`
- **CppNCorr** — owner/repo `xdic-dev/CppNCorr` (the submodule; a separate repo)

The code-side files (workflows, `Doxyfile`, `codecov.yml`, README badges) are
already committed for CPPxDIC. The equivalents for CppNCorr are staged in
`deploy/cppncorr-badges/` — copy them into a clone of CppNCorr first (see that
folder's `README.md`).

Providers in use: **Codecov** (coverage), **Doxygen → GitHub Pages** (API docs),
**GitHub Releases** (tags + auto-generated notes).

---

## 0. What was added to CPPxDIC

| File | Purpose |
|------|---------|
| `README.md` (badge row) | CI / coverage / docs / release / license badges |
| `.github/workflows/ci.yml` | `build-test`, `lint`, and `coverage` jobs |
| `.github/workflows/docs.yml` | Build Doxygen, publish to GitHub Pages |
| `.github/workflows/release.yml` | Create a GitHub Release on `v*` tags |
| `.github/release.yml` | Categorise PRs in auto-generated release notes |
| `codecov.yml` | Codecov behaviour (non-blocking, ignores submodules) |
| `Doxyfile` | Doxygen config (project `CPPxDIC`, README as main page) |
| `docs/PROJECT_INFRA_SETUP.md` | This guide |
| `deploy/cppncorr-badges/**` | Ready-to-apply copies for the CppNCorr repo |

> **Note on the License badge:** there is currently **no `LICENSE` file** in
> CPPxDIC. The `shields.io/github/license` badge will show "license not found"
> (or a dash) until you add a `LICENSE` file at the repo root and push it. Add
> the license your project actually uses (the README mentions it "follows the
> same license as ncorr / Matlab xDIC" — pick the concrete license and commit a
> `LICENSE` file). The same applies to CppNCorr.

---

## 1. Codecov (coverage)

Do this once per repo (CPPxDIC, then CppNCorr).

1. Go to <https://app.codecov.io> and **sign in with GitHub**.
2. Authorise Codecov for the `xdic-dev` organisation if prompted.
3. Click **Add new repository** (or go to
   `https://app.codecov.io/gh/xdic-dev/CPPxDIC`) and select the repo.
4. Codecov shows a **repository upload token** (`CODECOV_TOKEN`). Copy it.
5. Add it as an **Actions secret** in the repo:
   - GitHub → the repo → **Settings** → **Secrets and variables** → **Actions**
   - **New repository secret**
   - Name: `CODECOV_TOKEN`
   - Value: the token from step 4 → **Add secret**
6. Repeat steps 3–5 for `xdic-dev/CppNCorr` (its own token).

Notes:
- The `coverage` job builds with `--coverage`, runs `ctest`, generates
  `coverage.xml` with `gcovr`, and uploads via `codecov/codecov-action@v4`.
- `fail_ci_if_error: false` means a missing token on a fork PR will **not** fail
  CI. On your own org pushes the token is present, so uploads succeed.
- `codecov.yml` marks coverage status as `informational`, so coverage changes
  won't block PRs while the suite is still growing.

---

## 2. GitHub Pages (Doxygen API docs)

Do this once per repo.

1. GitHub → the repo → **Settings** → **Pages**.
2. Under **Build and deployment → Source**, choose **GitHub Actions**
   (not "Deploy from a branch").
3. Save. There is nothing else to configure — the `docs.yml` workflow uses
   `actions/upload-pages-artifact@v3` + `actions/deploy-pages@v4` with the
   `pages: write` / `id-token: write` permissions.
4. The workflow runs on every push to `main` (and manual dispatch). It runs
   `doxygen Doxyfile`, which writes HTML to `docs/doxygen/html`, uploads it as a
   Pages artifact, and deploys it.

Resulting URL pattern (organisation/project Pages):

- CPPxDIC → `https://xdic-dev.github.io/CPPxDIC/`
- CppNCorr → `https://xdic-dev.github.io/CppNCorr/`

The **Docs** badge links to that URL. (It is a static "docs: Doxygen" badge —
it turns into a working link as soon as the first Pages deploy succeeds.)

---

## 3. Releases (tags + auto notes)

To cut a release, push an annotated `vX.Y.Z` tag to the repo:

```bash
git tag -a v1.0.0 -m "CPPxDIC v1.0.0"
git push origin v1.0.0
```

What happens:
- `.github/workflows/release.yml` triggers on the `v*` tag.
- It runs `softprops/action-gh-release@v2` with `generate_release_notes: true`,
  which creates a GitHub Release for that tag with notes auto-built from the
  merged PRs since the previous tag.
- A tag containing a hyphen (e.g. `v1.0.0-rc1`) is marked as a **pre-release**.
- `.github/release.yml` controls how PRs are grouped in the notes (Features,
  Bug Fixes, Documentation, CI/Build, etc.) via PR **labels**. Label your PRs
  (`feature`, `bug`, `docs`, `ci`, …) to get tidy categorised notes.

The **Release** badge resolves to the latest published release tag once one
exists; before that it shows "no releases".

No secret is needed — `GITHUB_TOKEN` is provided automatically to the workflow.

---

## 4. Branch protection / required checks (recommended, optional)

GitHub → the repo → **Settings** → **Branches** → **Add branch ruleset**
(or classic **Branch protection rule**) for `main`:

- Require a pull request before merging.
- **Require status checks to pass** → select:
  - `Build & Test (Ubuntu)`
  - `Lint (clang-format)`
  - (Coverage is informational; you *can* require it but it's not recommended
    while the test suite is still being built out.)
- Optionally require branches to be up to date before merging.

This keeps `main` green and makes the CI badge meaningful.

---

## 5. Badge markdown (exact)

### CPPxDIC — already in `README.md`

```markdown
[![CI](https://github.com/xdic-dev/CPPxDIC/actions/workflows/ci.yml/badge.svg)](https://github.com/xdic-dev/CPPxDIC/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/xdic-dev/CPPxDIC/branch/main/graph/badge.svg)](https://codecov.io/gh/xdic-dev/CPPxDIC)
[![Docs](https://img.shields.io/badge/docs-Doxygen-blue)](https://xdic-dev.github.io/CPPxDIC/)
[![Release](https://img.shields.io/github/v/release/xdic-dev/CPPxDIC?include_prereleases&sort=semver)](https://github.com/xdic-dev/CPPxDIC/releases)
[![License](https://img.shields.io/github/license/xdic-dev/CPPxDIC)](https://github.com/xdic-dev/CPPxDIC/blob/main/LICENSE)
```

### CppNCorr — paste into its `README.md`

```markdown
[![CI](https://github.com/xdic-dev/CppNCorr/actions/workflows/ci.yml/badge.svg)](https://github.com/xdic-dev/CppNCorr/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/xdic-dev/CppNCorr/branch/main/graph/badge.svg)](https://codecov.io/gh/xdic-dev/CppNCorr)
[![Docs](https://img.shields.io/badge/docs-Doxygen-blue)](https://xdic-dev.github.io/CppNCorr/)
[![Release](https://img.shields.io/github/v/release/xdic-dev/CppNCorr?include_prereleases&sort=semver)](https://github.com/xdic-dev/CppNCorr/releases)
[![License](https://img.shields.io/github/license/xdic-dev/CppNCorr)](https://github.com/xdic-dev/CppNCorr/blob/main/LICENSE)
```

### How each badge turns green

| Badge | Goes green / live when |
|-------|------------------------|
| **CI** | The `ci.yml` workflow runs on `main` and passes. |
| **codecov** | A push to `main` runs the `coverage` job and Codecov receives an upload (needs `CODECOV_TOKEN`). |
| **Docs** | First successful `docs.yml` Pages deploy (link target becomes live). |
| **Release** | First `vX.Y.Z` tag is pushed and the Release workflow publishes it. |
| **License** | A `LICENSE` file exists at the repo root on `main`. |

---

## 6. End-to-end checklist

Per repo (CPPxDIC and CppNCorr):

- [ ] (CppNCorr only) Copy files from `deploy/cppncorr-badges/` into the CppNCorr
      repo and commit them; paste the badge snippet into its README.
- [ ] Add `CODECOV_TOKEN` secret (Settings → Secrets and variables → Actions).
- [ ] Settings → Pages → Source = **GitHub Actions**.
- [ ] (Optional) Add a `LICENSE` file so the license badge resolves.
- [ ] Push to `main` → CI + Coverage + Docs run; CI and codecov badges populate.
- [ ] (Optional) Add branch protection requiring `Build & Test` and `Lint`.
- [ ] Push a `vX.Y.Z` tag → Release is created; Release badge populates.
