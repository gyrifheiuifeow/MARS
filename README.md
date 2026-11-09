# MARS

Efficient Multi-Aspect Road-Constrained Vector Search

MARS searches for semantically similar objects within a road-distance budget.
It uses posting lists for frequent vectors and a semantic graph for the remaining
vectors. The code here uses α=64 and includes RoadScan, HNSW, SymphonyQG and
Mesh-Road for comparison.

### Compilation

Use Linux x86-64 with AVX2/FMA, Python 3.12+, and a C++17 compiler with OpenMP.
We used GCC 13.3. The Python scripts use the standard library. Queries run on
the CPU with the supplied 768-dimensional embeddings.

```bash
cd MARS
python3 scripts/build.py
```

The executables go in `build/bin/`. To run the small test cases:

```bash
python3 -m unittest discover -s tests -v
build/bin/mars_accept unit build/toy_acceptance
```

The C++ tests check vector partitioning, object associations, scores and
save/load behavior. They print `TOY_ACCEPTANCE_PASS` on success.

### Execution

Put the prepared data in `../mars-artifacts/MARSExp2`, following the layout in
[DATA.md](docs/DATA.md). This example builds one NYC index and runs its query
configurations:

```bash
python3 scripts/verify_data.py --data-root ../mars-artifacts/MARSExp2

python3 scripts/reproduce.py run \
  --method MARS --region NYC --rep 1 --stage build \
  --data-root ../mars-artifacts/MARSExp2 --output-root ../mars-run

python3 scripts/reproduce.py run \
  --method MARS --region NYC --rep 1 --stage perf \
  --data-root ../mars-artifacts/MARSExp2 --output-root ../mars-run
```

The index goes in `../mars-run/instances/MARS/NYC/rep1`. Query results go in
`../mars-run/results/MARS/NYC/rep1/perf`. Both commands use the same output root.
For another method, replace `MARS` in both commands with `HNSW`, `SymphonyQG`,
`Mesh-Road` or `RoadScan`.

To run the MARS matrix across four regions and three index instances per region:

```bash
python3 scripts/run_matrix.py \
  --data-root ../mars-artifacts/MARSExp2 \
  --output-root ../mars-full --execute
```

This runs 144 performance tasks and 48 phase-diagnostic tasks in sequence.
Omit `--execute` to prepare the task files and inspect the plan first.
The α sweep, Graph-only experiment and extra query-group runs have their own
entries in [EXPERIMENTS.md](docs/EXPERIMENTS.md) and [ARCHIVE.md](docs/ARCHIVE.md).

Saved tables and per-query CSVs are in [`results/alpha64/`](results/alpha64/)
and [`results/query_groups_four_regions/`](results/query_groups_four_regions/).
They cover query efficiency, the effect of τ, Pareto curves, popular/medium/rare
queries, α and ef, the three ablations, and index build time and size.
You can check the saved results without building an index:

```bash
python3 scripts/verify_sources.py
python3 scripts/verify_raw_results.py
python3 scripts/verify_results.py
```

### Parameters

The defaults are α=64, k=10 and rest ef=1600. Frequency counts distinct objects
after merging equal normalized vectors. Vectors associated with at least α
objects enter pivot. The rest graph has one node per remaining vector/object
association.

| Argument | Use |
|---|---|
| `--method`, `--region` | Choose the method and one of `NYC`, `BAY`, `COL`, `FLA`. |
| `--data-root`, `--output-root` | Set the input location and run directory. Use a new output root for a new experiment. |
| `--rep` | Choose index instance 1, 2 or 3. Build each instance separately. |
| `--alpha` | Set the frequency threshold. Use a separate output root for each value. |
| `--stage` | Select a stage such as `build`, `perf` or, for MARS, `diag`. |
| `--dry-run` | Prepare the task files and print the command. |

`configs/mars/performance_matrix.json` lists radii, ef values, query groups and
variants. Baseline settings are in `configs/baselines/`. The `pivot` variant
disables rest, and `pointwise` uses direct road-distance checks. For the
Graph-only pivot ablation, use [`scripts/graph_only.py`](scripts/graph_only.py)
with the commands in [ARCHIVE.md](docs/ARCHIVE.md).

QPS uses the total core query time. Recall uses the score-threshold evaluator
with denominator `min(k, GT count)`. Tables give means and sample standard
deviations over index instances. [EXPERIMENTS.md](docs/EXPERIMENTS.md) records
the timing boundaries and the conditions of the historical runs.

### Datasets

| Region | Objects | Directory |
|---|---:|---|
| `NYC` | 1,140,399 | `data/NYC` |
| `BAY` | 700,864 | `data/BAY` |
| `COL` | 393,709 | `data/COL` |
| `FLA` | 1,534,361 | `data/FLA` |

Each region contains its road graph, POI mapping, name and category vectors,
query vectors and ground truth. The four compressed region archives total
about 8.84 GB. Allow more space for extraction and indexes.

Build ANN indexes locally. Graph construction uses random choices, so a rebuilt
index can produce different approximate answers from the saved instances.
The archive also contains the earlier category-based MARS implementation and
other α runs.
