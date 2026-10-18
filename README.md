# MARS

Efficient Multi-Aspect Road-Constrained Vector Search

MARS retrieves objects by semantic similarity within a road-distance budget.
It combines road predicates, pivot posting lists, and a semantic graph to search
objects described by multiple aspects. This repository contains the MARS
implementation, four comparison methods, experiment scripts, prepared datasets,
and per-query results.

### Compilation

Use Linux x86-64 with AVX2/FMA, Python 3.12 or later, and a C++17 compiler with
OpenMP. The experiments used GCC 13.3. Python scripts use the standard library.
Search with the supplied embeddings runs on the CPU.

```bash
cd MARS
python3 scripts/build.py
```

The build compiles MARS, the baseline runner, and the validation programs into
`build/bin/`. Third-party source and its notices are included in the repository.

### Execution

Run the small correctness checks first. They do not require a dataset or a
constructed index:

```bash
python3 -m unittest discover -s tests -v
build/bin/mars_accept unit build/toy_acceptance
```

The C++ checks cover frequency partitioning, object associations, scoring, and
index serialization. A successful run prints `TOY_ACCEPTANCE_PASS`.

Place the prepared data under `../mars-artifacts/MARSExp2`. The following
commands build one MARS index for NYC and run its performance configurations.
Replace `NYC` with another region from the dataset table to run that region.

```bash
python3 scripts/verify_data.py --data-root ../mars-artifacts/MARSExp2

python3 scripts/reproduce.py run \
  --method MARS --region NYC --rep 1 --stage build \
  --data-root ../mars-artifacts/MARSExp2 --output-root ../mars-run

python3 scripts/reproduce.py run \
  --method MARS --region NYC --rep 1 --stage perf \
  --data-root ../mars-artifacts/MARSExp2 --output-root ../mars-run
```

The index is written to `../mars-run/instances/MARS/NYC/rep1`, and query results
to `../mars-run/results/MARS/NYC/rep1/perf`. Use the same output root for the build
and query stages. To run a comparison method, replace `MARS` in both commands
with `HNSW`, `SymphonyQG`, `Mesh-Road`, or `RoadScan`.

To run the full MARS experiment matrix with three independently constructed
indexes per region:

```bash
python3 scripts/run_matrix.py \
  --data-root ../mars-artifacts/MARSExp2 \
  --output-root ../mars-full --execute
```

Without `--execute`, this command prepares the task files and prints the plan.
The matrix includes 144 performance runs and 48 separate diagnostic runs. The
additional α sweep, Graph-only ablation, and four-region query-group experiments
are described in [the experiment protocol](docs/EXPERIMENTS.md) and
[the archive guide](docs/ARCHIVE.md).

Recorded results are available in [`results/alpha64/`](results/alpha64/) and
[`results/query_groups_four_regions/`](results/query_groups_four_regions/).
They cover query efficiency, QPS and Recall as τ varies, Pareto curves, query
groups, parameter studies, ablations, and index construction costs. To check
the raw files and recalculate the reported statistics:

```bash
python3 scripts/verify_sources.py
python3 scripts/verify_raw_results.py
python3 scripts/verify_results.py
```

QPS is calculated from the total core query time. Recall uses the recorded
score-threshold criterion, with a denominator of `min(k, ground-truth count)`.
Tables report means and sample standard deviations across index instances.
Timing conventions, baseline settings, and index-size definitions are given in
[the protocol](docs/EXPERIMENTS.md); CPU bindings and historical run conditions
are retained with the experiment records.

### Parameters

- `--method` selects MARS or one of the four comparison methods. `--region`
  selects `NYC`, `BAY`, `COL`, or `FLA`.
- `--data-root` points to the directory containing `data/<region>`.
  `--output-root` selects the directory for indexes, logs, and query results.
  Use a new output directory for a new experiment.
- `--rep` selects index instance 1, 2, or 3. Each instance is constructed
  separately and reused across its query configurations.
- `--alpha` sets the MARS frequency threshold. Vectors belonging to at least
  α distinct objects enter the pivot channel; the remaining vectors enter
  the rest graph. The default is 64. Use a separate output root for each α.
- The default query settings are `k=10` and rest `ef=1600`. Radius, ef, workload,
  and ablation variants are recorded in `configs/mars/performance_matrix.json`.
  Baseline configurations are under `configs/baselines/`.
- `--stage diag` runs the MARS phase measurements separately from performance
  timing. `--dry-run` prepares the inputs and prints the command without
  building an index or running queries.

The rest ablation uses the `pivot` variant; the road-compilation ablation uses
`pointwise`. Both are included in the MARS matrix. The pivot ablation uses the
shared-category Graph-only implementation, prepared and run through
[`scripts/graph_only.py`](scripts/graph_only.py). Its commands are listed in
[the archive guide](docs/ARCHIVE.md).

### Datasets

| Region argument | Objects | Data directory |
|---|---:|---|
| `NYC` | 1,140,399 | `data/NYC` |
| `BAY` | 700,864 | `data/BAY` |
| `COL` | 393,709 | `data/COL` |
| `FLA` | 1,534,361 | `data/FLA` |

Each region contains road graphs, POI mappings, 768-dimensional name and
category vectors, query vectors, and ground truth. The experiments use the
supplied vectors directly. File layouts and input hashes are described in
[DATA.md](docs/DATA.md).

The four compressed region archives total about 8.84 GB. Additional storage is
needed for extraction and index construction. Generated ANN indexes are built
locally; their historical build records and file hashes are included in the
archives. Fresh graph construction may produce different index bytes and
approximate query results.

The main implementation uses α64;
the original category-based version and earlier α experiments are retained
separately.
