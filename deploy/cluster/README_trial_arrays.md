# Trial-level experiment runs on a SLURM cluster

This directory holds ready-to-run `sbatch` array scripts for running the CPPxDIC
pipeline at the **trial level**, plus the workflow for generating the
`subject_trial.csv` that drives the (subject, trial) array mode.

There are three ways to parameterise a run, all handled by the `cppxdic`
binary's CLI:

| Mode | Flag(s)                              | What a task runs                                            |
|------|--------------------------------------|------------------------------------------------------------|
| (a)  | `--trial <id>`                       | a single trial                                             |
| (b)  | `--trials <list>` / `--trials-file`  | the `SLURM_ARRAY_TASK_ID`-th trial of the list             |
| (c)  | `--subject-trial-csv <path>`         | the `SLURM_ARRAY_TASK_ID`-th (subject, trial) row of a CSV |

When `SLURM_ARRAY_TASK_ID` is **unset**, modes (b)/(c) run the whole list
(mode (c) requires all rows to share one subject); this is handy for local
smoke tests.

---

## 1. Generate `subject_trial.csv`

`gen_subject_trial` enumerates each subject's trials by reusing the same
protocol-`.mat` lookup the pipeline uses (`DicAnalysis::searchTrialTarget`).
It is OFF by default; enable it at configure time:

```bash
cmake -S . -B build_gst -DBUILD_GEN_SUBJECT_TRIAL=ON
cmake --build build_gst --target gen_subject_trial -j

# Subjects inline:
./build_gst/gen_subject_trial --subjects S08,S09,S10 -o subject_trial.csv

# ...or from a file (one subject id per line, # = comment):
./build_gst/gen_subject_trial --subjects-file subjects.txt -o subject_trial.csv

# ...or as positional args:
./build_gst/gen_subject_trial S08 S09 S10
```

The generator reuses the same `*_params.txt` configuration files as `cppxdic`
(override with `-d/-n/-v`), so paths, phase, and the nf/spd condition sets are
identical to a real run.

Output `subject_trial.csv` (one pair per line, header included):

```csv
subject,trial
S08,7
S08,12
S09,7
S09,12
S09,25
S10,7
```

On exit it also prints the exact array size to use, e.g.
`SLURM array size for subject_trial mode: --array=1-6`.

---

## 2. Run a single trial (mode a)

No array needed:

```bash
./build/cppxdic --subject S09 --trial 7
```

---

## 3. SLURM array over a trial list (mode b)

`run_trials_array.sbatch` runs one trial per task; the subject is fixed.

```bash
# Inline list of 3 trials -> array 1-3
sbatch --array=1-3 \
       --export=ALL,SUBJECT=S09,TRIALS="7,12,25" \
       deploy/cluster/run_trials_array.sbatch

# From a trials file -> size the array from the line count
N=$(grep -vcE '^\s*(#|$)' trials.txt)   # non-blank, non-comment lines
sbatch --array=1-${N} \
       --export=ALL,SUBJECT=S09,TRIALS_FILE=trials.txt \
       deploy/cluster/run_trials_array.sbatch
```

`cppxdic --subject S09 --trials 7,12,25` reads `SLURM_ARRAY_TASK_ID` and runs
the Nth (1-based) trial of the list.

---

## 4. SLURM array over (subject, trial) pairs (mode c)

`run_subject_trial_array.sbatch` runs one (subject, trial) pair per task. The
array size is the number of **data** rows in the CSV (total lines minus the
header):

```bash
# Robust to blank/comment lines and a missing trailing newline:
N=$(grep -vcE '^\s*(#|subject,|$)' subject_trial.csv)
# (or simply: N=$(($(wc -l < subject_trial.csv) - 1)) for a clean CSV)

sbatch --array=1-${N} \
       --export=ALL,SUBJECT_TRIAL_CSV=subject_trial.csv \
       deploy/cluster/run_subject_trial_array.sbatch
```

`cppxdic --subject-trial-csv subject_trial.csv` reads `SLURM_ARRAY_TASK_ID`,
skips the header, and selects the Nth data line, setting both the subject and
the trial for that task.

---

## Singularity

Both scripts honour `SINGULARITY_IMAGE=<path-to.sif>`; when set they invoke
`singularity exec --env SLURM_ARRAY_TASK_ID=... <image> cppxdic ...` instead of
the host binary. `SLURM_ARRAY_TASK_ID` is forwarded into the container so the
in-binary row/trial selection works identically. Example:

```bash
sbatch --array=1-${N} \
       --export=ALL,SUBJECT_TRIAL_CSV=subject_trial.csv,SINGULARITY_IMAGE=cppxdic.sif \
       deploy/cluster/run_subject_trial_array.sbatch
```

## Common overrides

| Variable             | Default            | Meaning                                          |
|----------------------|--------------------|--------------------------------------------------|
| `CPPXDIC_BIN`        | `./build/cppxdic`  | path to the host `cppxdic` binary                |
| `SINGULARITY_IMAGE`  | *(empty)*          | if set, run via `singularity exec` of this `.sif`|
| `WORKDIR`            | submit dir         | dir holding `*_params.txt` (and the CSV)         |
| `SUBJECT`            | `S09`              | subject for mode (b)                             |
| `TRIALS`/`TRIALS_FILE` | *(empty)*        | trial list / trials file for mode (b)            |
| `SUBJECT_TRIAL_CSV`  | `subject_trial.csv`| CSV for mode (c)                                 |

Tune `--time`, `--cpus-per-task`, and `--mem` in the `#SBATCH` headers (or
override on the `sbatch` command line) to match your job's needs.
