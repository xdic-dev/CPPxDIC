# CPPxDIC Refactoring Plan — Round 2 (`ja/refactoring/2`)

Status: **proposal** — working plan for the second refactoring round. It
supersedes the stale `ja/refactoring/1` branch (PR #11), which forked at
`e7723be`, before the newversion restructure (#21), SLURM/staged pipeline
(#24, #32), leveled logging (#29), webgui (#31), video-import perf (#35),
optional apps (#36), matlab-parallel singledic/proxyncorr (#37), and the
in-flight cuNCorr engine integration.

A companion plan exists in the CppNCorr submodule
(`Tools/CppNCorr/docs/refactoring_plan.md`, branch `ja/refactoring/2` there).

## 1. Where round 1 stands

`ja/refactoring/1` (10 commits, 63 files, +6097/−4847) moved toward a layered
`cppxdic::` namespace tree (`app/`, `domain/`, `io/mat/`, `pipeline/`,
`visualization/`):

- **Step F** extracted into a pipeline-runner architecture
  (`step_f_runner.cpp`, ~678 ln out of `dic_analysis.cpp`).
- **Step D / DIC2D** decomposed into services: matching, tracking,
  frame-preparer, ncorr-runner, output-formatter (~900 ln out of
  `step_d_workflow.cpp`).
- **MAT I/O** split into 5 components: `matio_helpers`, `mat_ncorr_writer`,
  `mat_ncorr_codecs`, `mat_results_writer`, `mat_result_codecs`
  (~1500 ln out of `mat_writer.cpp`), plus guard tests.

It was ~70% complete (extractions implemented and wired for step F; top-level
`PipelineRunner`/`main.cpp` integration unfinished). **It cannot be rebased**:
main rewrote every file it touched (staging/checkpoints threaded through
step D, mode dispatch in `main.cpp`/`dic_analysis.cpp`, new serialization in
`mat_writer.cpp`, CMake grew from ~130 to 264 lines with new apps).

| Round-1 theme | Verdict |
|---|---|
| `matio_helpers` (low-level matio wrappers) | **Cherry-pick almost as-is** — clean, xDIC-agnostic utility layer |
| MAT codec/writer split | **Conceptually valid, redo** against today's 2721-line `mat_writer.cpp` (+ #36/#37 formats) |
| DIC2D service decomposition | **Conceptually valid, redo** — interfaces are good specs, but must thread stage plans / `only_cam` narrowing (#32) |
| Checkpoint policy idea | **Valid and now more urgent** — staging (#32) multiplied ad-hoc checkpoint path logic |
| `PipelineRunner` top-level abstraction | **Drop for now** — too thin for today's mode × stage dispatch |
| `cppxdic::` namespace tree + CMake restructure | **Defer** — do extractions first, namespace later |
| Guard tests (`test_refactor_guards.cpp`) | **Merge patterns** into the existing test suite |

## 2. Current pain points on `main`

~27 source files, 14k+ LOC. God-files and their tangled concerns:

- `src/mat_writer.cpp` — **2721 lines** (header: 555 ln, 40+ methods): matio
  plumbing + struct builders + ncorr encoding + 3D/deformation codecs, no
  layering, untestable in isolation.
- `src/dic_analysis.cpp` — **1774 lines**: step E/F orchestration + mode
  dispatch + ad-hoc checkpoint checks.
- `src/step_d_workflow.cpp` — **1352 lines**: setup, frame I/O, ROI/seed,
  matching, tracking, formatting — with stage narrowing (`plan.only_cam`)
  threaded through everything.
- `src/visualization.cpp` — **1318 lines**: debug panels, colormaps, field
  exports mixed with file I/O.
- **Checkpoint logic duplicated** across `step_d_workflow.cpp` and
  `dic_analysis.cpp` (inline `std::filesystem::exists(...)` per stage file).
- **Engine seam is new**: with cuNCorr integrated as an alternative DIC
  engine, the adapter boundary (what CPPxDIC asks of an engine) should be an
  explicit, tested interface rather than call sites into CppNCorr types.

## 3. Plan

Ordering principle: **extract first, namespace later**; every step is a
behavior-preserving move validated by the existing test suite + staged-pipeline
scripts. Coordinate with `feat/cuncorr-integration` — land or rebase around it
before touching the DIC2D call sites it modifies.

### Phase 1 — foundations (low risk, unblocks the rest)

1. **`matio_helpers` extraction** — cherry-pick from round 1
   (`63cb921`: createStruct/addField/createCellArray*/write*Variable) into
   `include/io/mat/` + `src/io/mat/`, flat namespace for now. (~1–2 days)
2. **`CheckpointPolicy`** — adapt round 1's `checkpoint_policy.h` as the spec;
   centralize "which stage artifacts exist / what can run next" for
   MATCHING2 / ncorr{1,2}.bin / dic_info paths used by #32 SLURM staging.
   Replace inline existence checks in `step_d_workflow.cpp` and
   `dic_analysis.cpp`. Must reproduce legacy paths exactly. (~3–5 days)

### Phase 2 — MAT I/O codecs (highest LOC payoff)

3. Split `mat_writer.cpp` (2721 → ~600 ln dispatcher) into
   `src/io/mat/`: `mat_ncorr_codec` (2D DIC outputs), `mat_results_codec`
   (3D + deformation/strain), `mat_schema` (struct field schemas), on top of
   `matio_helpers`. Round 1's codec structure is the template; today's
   #36/#37 output modes are the source of truth. Binary-format stability
   guarded by `tests/test_mat_writers.cpp` + `test_var_parity.cpp` and new
   guard tests ported from round 1. (~5–7 days)

### Phase 3 — DIC2D services (reuse for singledic/proxyncorr/cuNCorr)

4. Decompose `step_d_workflow.cpp` (1352 → ~300 ln orchestration) into
   `frame_preparer`, `roi_seed_initializer`, `dic2d_matching`,
   `dic2d_tracking` — round 1 headers as interface specs, with stage
   narrowing passed explicitly (StagePlan in, not flags threaded through).
5. **Engine interface** — formalize the DIC-engine seam introduced by the
   cuNCorr integration: one adapter interface both CppNCorr and cuNCorr
   implement, unit-tested with a fake engine.

### Phase 4 — supporting cleanups (independent, low risk)

6. **Visualization**: extract `colormap`, `panel_builder`, `field_exporter`
   from `visualization.cpp` (1318 → ~1000 ln); reuse in webgui/proxyncorr.
7. **Config/parameter validation layer**: `ConfigValidator` /
   `ParameterValidator` returning structured errors; single home for
   cross-parameter checks (incl. engine availability, e.g. cuNCorr present).
8. **Image/ROI utility consolidation**: `image_processor`, `roi_manager`,
   frame loading under one `image/` module; one home for saturation
   thresholds.

### Deferred

- `cppxdic::` namespace hierarchy + CMake restructure — after phases 1–3.
- Top-level `PipelineRunner` — revisit once mode × stage dispatch stabilizes.
- Full rebase/merge of `ja/refactoring/1` — not feasible; branch is mined for
  the pieces above, then PR #11 can be closed.

### Guardrails

- One extraction per PR-sized commit; tests + staged-pipeline smoke runs green
  at each step.
- No behavior change; MAT binary output byte-compatibility checked where
  parity tests exist.
- Checkpoint file names/paths are a compatibility contract with SLURM scripts
  and MATLAB tooling — never renamed during refactoring.
