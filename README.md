# MARS: Efficient Multi-Aspect Road-Constrained Vector Search

MARS searches for objects that are semantically relevant to a query and reachable within a road-distance budget. This repository contains the implementation, prepared inputs, and experiment records used during development. The main entry point follows the **October 17, 2026 frequency-partitioned version, with α = 64 and ef = 1600**. The earlier category-based implementation and the other parameter settings are retained so that the reported results can be traced back to the code that produced them.

[中文说明](README.zh-CN.md) · [Experiment guide](docs/EXPERIMENTS.md) · [Version history](docs/VERSIONS.md) · [Data and full archives](docs/ARCHIVE.md)

## Start with the recorded results

The final tables and their per-query CSV files are in the repository. You can check the reported values without building an index or downloading the vector datasets:

```bash
git clone https://github.com/gyrifheiuifeow/MARS__Efficient_Multi_Aspect_Road_Constrained_Vector_Search.git
cd MARS__Efficient_Multi_Aspect_Road_Constrained_Vector_Search
python3 scripts/verify_sources.py
python3 scripts/verify_raw_results.py
python3 scripts/verify_results.py
```

The checks compare raw-file SHA-256 hashes, recalculate QPS and Recall, and recompute the means and sample standard deviations over index instances. They cover 918 raw files and 1,065 table-to-file bindings. Several experiments reuse the same configuration, so these bindings are not 1,065 independent measurements.

| What you are looking for | Where to start |
|---|---|
| Query efficiency, QPS/Recall as τ changes, and Pareto curves | [`results/alpha64/Exp1_3_final_summary.csv`](results/alpha64/Exp1_3_final_summary.csv) |
| Popular, medium and rare queries across four regions and five methods | [`results/query_groups_four_regions/`](results/query_groups_four_regions/) |
| α parameter study, 11 values and three instances per value | [`Alpha_parameter_summary.csv`](results/alpha64/Alpha_parameter_summary.csv) |
| ef parameter study for α64 | [`Exp4_MARS_summary.csv`](results/alpha64/Exp4_MARS_summary.csv) |
| Removing pivot: shared-category Graph-only | [`Graph_only_summary.csv`](results/alpha64/Graph_only_summary.csv) |
| Removing rest: full versus Pivot-only | [`Exp5_MARS_summary.csv`](results/alpha64/Exp5_MARS_summary.csv) and [`Exp6_MARS_summary.csv`](results/alpha64/Exp6_MARS_summary.csv) |
| Road compilation versus direct distance checks | [`Exp7_paired_road_summary.csv`](results/alpha64/Exp7_paired_road_summary.csv) |
| Index build time, serialization, size and peak memory | [`Exp8_final_cost_summary.csv`](results/alpha64/Exp8_final_cost_summary.csv) |

Names such as “Exp4” and “Exp5” come from the saved campaigns; their numbering changed during paper revisions. The descriptions above are the intended guide. In particular, the October 17 category-based ef remeasurement is a separate historical experiment, not the α64 ef curve.

## Build and run

The search code targets Linux x86-64 with AVX2/FMA, C++17 and OpenMP. The recorded compiler is GCC 13.3. The Python tools use the standard library; Python 3.12 or later is needed by the archive extractor. A GPU is not required for search with the supplied embeddings. [`Dockerfile`](Dockerfile) describes an Ubuntu 24.04 build environment, and the measurement machine is recorded in [`provenance/build_environment.json`](provenance/build_environment.json).

```bash
python3 scripts/build.py
python3 -m unittest discover -s tests -v
build/bin/mars_accept unit build/toy_acceptance
python3 scripts/fetch_artifacts.py --kind data --output ../mars-artifacts
python3 scripts/verify_data.py --data-root ../mars-artifacts/MARSExp2
```

The downloader obtains the four prepared datasets and additional ground truth from this repository's [release](https://github.com/gyrifheiuifeow/MARS__Efficient_Multi_Aspect_Road_Constrained_Vector_Search/releases/tag/v2026.10.18-reproducibility), joins split assets and verifies their checksums before extraction. If the repository is private, use a GitHub account with access; the downloader accepts `GITHUB_TOKEN` or the existing Git credential helper. Do not put a token in a script or commit it.

Build one instance, then query that saved instance:

```bash
python3 scripts/reproduce.py run --method MARS --region NYC --rep 1 \
  --stage build --data-root ../mars-artifacts/MARSExp2 --output-root ../mars-run
python3 scripts/reproduce.py run --method MARS --region NYC --rep 1 \
  --stage perf --data-root ../mars-artifacts/MARSExp2 --output-root ../mars-run
```

`--method` also accepts HNSW, SymphonyQG, Mesh-Road and RoadScan. The MARS performance matrix includes ef, rest and road-compilation variants. To plan the complete MARS matrix, use `scripts/run_matrix.py` with the same two root arguments; add `--execute` to construct the twelve instances and run the matrix serially. Existing output stages are never silently replaced. `reproduce.py --alpha` selects a different frequency threshold in a separate output tree. [`graph_only.py`](scripts/graph_only.py) prepares and compiles the archived Graph-only sources in a fresh tree. Additional query-group and historical campaign entry points are indexed in [the archive guide](docs/ARCHIVE.md).

## Reading the numbers

QPS is the number of queries divided by the sum of core query times. Recall uses the recorded score-threshold evaluator and its original ground-truth denominator. Diagnostic runs measure additional phases separately and are not used for performance QPS. The accepted HNSW version includes the empty-candidate restart; RoadScan uses the recorded approximate scoring configuration, so its Recall should not be assumed to be 100% simply because it scans the road range.

Build time, saving time and memory are separate quantities. The tables retain both serialized bytes and structural bytes after subtracting explicit copies of shared vector payloads. Structural bytes are not the size of the files on disk.

There are also limits to the historical comparisons. Graph-only used CPU96/NUMA4, while the main MARS campaign used an unpinned single thread. The additional BAY/COL/FLA query-group runs were pinned, whereas NYC reuses the earlier measurements. Ten historical baseline run windows recorded other active processes. The α=2 point uses an earlier specialized implementation. These facts are preserved with the results rather than hidden by averaging.

## What is preserved

The release includes the prepared road graphs, POI mappings, embeddings, queries and ground truth, plus the source trees, configurations, logs, intermediate results, audits and local plotting materials from earlier rounds. The full archives retain original paths and file bytes; portable adapters live in `scripts/` and `runtime/`. See [the archive guide](docs/ARCHIVE.md) for the inventory and exclusions.

The almost-terabyte development tree contained many generated ANN indexes. Those large binary indexes are inventoried but are not distributed in this release. Their saved bindings and existing hash records are retained alongside the construction code. Rebuilding can produce a different approximate graph, even with the same seed label. The publication checks establish the integrity of the recorded inputs, code and results; they do not claim that every fresh index will reproduce every historical result bit for bit. This packaging pass did not rerun the full real-data benchmark.

Third-party code and notices are described in [THIRD_PARTY.md](docs/THIRD_PARTY.md). No new license grant for the prepared datasets is inferred from their inclusion here.
