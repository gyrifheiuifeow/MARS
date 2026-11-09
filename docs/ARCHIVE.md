# Archived runs

Use the checkout's `runtime/` and `scripts/` for the current entry points. The
historical archives contain the earlier source trees, run configurations, logs
and intermediate results. These attachments need separate redaction for review,
as described in [ANONYMOUS.md](ANONYMOUS.md).

To restore locally supplied archives:

```bash
python3 scripts/fetch_artifacts.py --kind all \
  --assets-dir /path/to/assets --output /path/to/artifacts
```

The script checks `provenance/release_assets.json`, joins split files and
extracts them. Each `.partNN` file is a consecutive byte range of one gzip
archive. The extraction can resume when existing files have matching bytes.
Use `--manifest` for archives with a different checksum manifest.

The restored data root is `/path/to/artifacts/MARSExp2`. `--kind data` selects
datasets and additional GT, `--kind records` selects server research records,
and `--kind local` selects local reports and plotting files. The four region
archives total about 8.84 GB compressed, and the server records about 2.76 GB.
Allow space for the compressed files, joined archives, extracted contents and
new indexes.

## Run directories

Paths below are relative to
`MARSExp2/submission_experiments_20260726/results/student_runs/`.

| Directory | Contents |
|---|---|
| `student_mars_alpha64_final_20261017_v1` | Main α64 sources, drivers, configurations, queries, measurements and costs. `final_delivery_v1/` holds the result package. `execution_adapter_v2/` contains the continuation and export fixes. |
| `student_query_groups_three_regions_alpha64_20261017_v1` | BAY/COL/FLA popular/medium/rare runs and their GT. `delivery_v1/` combines these with the NYC records. |
| `student_measure_entry_20261007_v3_exact_validation` | Baseline build/query sources, cache preparation, integer-road checks and build costs, before the HNSW retry patch. |
| `student_hnsw_empty_restart_candidate_20261008_v1` | HNSW empty-candidate restart and its remeasurement. |
| `student_formal_measurements_20261005_151357` | Earlier measurements and original-category MARS references. |
| `student_exp67_final_20261005_033224` | Original-category source for the rest and road experiments. |
| `student_exp6_original_groups_20261009_v2` | Original-category rest ablation on popular, medium and rare. The v1 directory is also included. |
| `student_graph_only_shared_category_nyc_20261016_v1` | Shared-category Graph-only construction, object associations and build checks. |
| `student_graph_only_shared_category_nyc_medium1000_20261016_v1` | Medium1000 Graph-only timing and cache checks. |
| `student_exp5_ef_rerun_20261017_v1` | Original-category ef remeasurement on three existing indexes. |
| `student_mars_freq_a2_v1` | Specialized α2 implementation. |
| `student_mars_freq_a32_formal_20261015_v1` | Earlier α32 campaign. |
| `student_mars_freq_general_*`, `student_mars_NYC_alpha*` and related pilot directories | Threshold trials and individual α results used by the final campaign. |

The α64 directory contains `run_campaign.py`, `run_extensions.py` and
`export_final.py`. Graph-only instances 2 and 3 have separate `runtime` and
`runtime_query` trees, with `run_shared.py`, `run_sweep.py` and their input
bindings. The final tables identify the reused first instance.

To trace a result, follow its `raw_file`, `raw_sha256`, `index_binding` or
`binding_file` columns. The archived launchers contain their original path,
resume and CPU/NUMA settings. Copy the needed inputs to a new output tree for
a rerun. A completed result directory already contains files that these
launchers expect to create.

## Graph-only

`scripts/graph_only.py` copies the shared-category sources into a new directory
and adjusts their compiler paths. Prepare and compile them first:

```bash
python3 scripts/graph_only.py prepare --workspace /path/to/artifacts/MARSExp2 --output /path/to/new-graph-run
python3 scripts/graph_only.py compile --workspace /path/to/artifacts/MARSExp2 --output /path/to/new-graph-run
```

Choose an available CPU and its NUMA node. The saved runs used CPU96/NUMA4:

```bash
python3 scripts/graph_only.py stats --workspace /path/to/artifacts/MARSExp2 --output /path/to/new-graph-run --cpu 96 --numa 4
python3 scripts/graph_only.py build --workspace /path/to/artifacts/MARSExp2 --output /path/to/new-graph-run --cpu 96 --numa 4
python3 scripts/graph_only.py reload --workspace /path/to/artifacts/MARSExp2 --output /path/to/new-graph-run --cpu 96 --numa 4
python3 scripts/graph_only.py perf --workspace /path/to/artifacts/MARSExp2 --output /path/to/new-graph-run --cpu 96 --numa 4 --ef 1600
```

Build and reload each run the 100-query checks. Performance uses medium1000
and compares cache state with the saved reference. Repeat `perf` at ef 3200
and 6400 on the same index. Build the other instances in separate directories.
The helper records CPU/NUMA settings. The archived wrappers also sampled other
processes and memory placement during the original runs.

The publication checks covered preparation and compilation. The archived CSVs
provide the measured Graph-only results.

## Files and checksums

`source_inventory.json` lists 45,836 regular files from the server workspace.
The research archive contains 42,994 of them, with paths, sizes, modification
times and SHA-256 hashes. This includes trial runs and diagnostics as well as
the final results. `symlinks.json` records file-symlink targets. Dataset hashes
are in `provenance/dataset_files.json`.

The inventory marks excluded files: generated `.bin` and `.hnsw` indexes,
compiled programs and libraries, bytecode, nested delivery archives and model
weights. The prepared embeddings support the saved workloads. The archive
also keeps the embedding generator for experiments that generate new vectors.

For excluded indexes, the inventory gives paths and sizes. The run directories
retain their historical index hashes and bindings. Those hashes refer to the
saved files on the experiment machine. Rebuild the indexes for a new run.

The local archive contains 9,504 files from experiment outputs, Graph-only
deliveries, plots, sample inputs and the October 15 publication checkout.
`local_files.json` gives their paths and hashes. It excludes bundled gnuplot
programs, duplicate archives, bytecode and generated indexes.

To check the extracted research files:

```bash
python3 scripts/verify_archive.py \
  --root /path/to/artifacts/MARSExp2 \
  --inventory /path/to/artifacts/source_inventory.json
```

Use the corresponding root and inventory for dataset or local-history files.
`verify_sources.py` checks the 787 source mappings. `verify_raw_results.py`
checks raw-query files, and `verify_results.py` recomputes summary statistics.

Old launchers and failure markers remain alongside later fixes. Read a run's
completion record and raw-file binding together to identify its final state.
