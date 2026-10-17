# Finding the code and data behind a result

The Git checkout is the working entry point for the α64 implementation and the final result tables. The release preserves the wider development history. Both are needed if the aim is to inspect an older experiment rather than rerun the current main matrix.

`python3 scripts/fetch_artifacts.py --kind all --output /path/to/artifacts` restores the release. It checks each asset against `provenance/release_assets.json` before extraction. Split files are ordinary consecutive byte ranges of a gzip archive, not independently decompressible gzip files. Use the downloader rather than unpacking one part by itself. The downloader will not replace an existing file with different bytes and can resume after an interrupted extraction when existing bytes match.

The data root after extraction is `/path/to/artifacts/MARSExp2`. `--kind data` restores the prepared datasets and additional GT; `--kind records` restores the research history; `--kind local` restores the local reports and plotting materials. The four dataset archives occupy about 8.84 GB compressed and the server history about 2.76 GB. Allow space for compressed downloads, joined archives, extracted files and any indexes that you intend to construct.

## Where the original campaigns live

All paths in this table are relative to `MARSExp2/submission_experiments_20260726/results/student_runs/` in the extracted research archive.

| Campaign directory | Contents and use |
|---|---|
| `student_mars_alpha64_final_20261017_v1` | Main α64 campaign: `src_tree/`, `drivers/`, `configs/`, `queries/`, `runs/`, cost records, `final_delivery_v1/`, and the freeze/audit files. `execution_adapter_v2/` records the accepted continuation and export fixes. |
| `student_query_groups_three_regions_alpha64_20261017_v1` | Additional BAY/COL/FLA popular/medium/rare runs and generated GT; `delivery_v1/` combines them with existing NYC records. |
| `student_measure_entry_20261007_v3_exact_validation` | Frozen baseline build/query trees, cache preparation, integer-road assertion, build costs and pre-HNSW-repair records. |
| `student_hnsw_empty_restart_candidate_20261008_v1` | Accepted HNSW empty-candidate restart source and formal remeasurement records. |
| `student_formal_measurements_20261005_151357` | Earlier formal measurements and original-category MARS references. |
| `student_exp67_final_20261005_033224` | Original-category source used by corrected rest and road experiments. |
| `student_exp6_original_groups_20261009_v2` | Original-category rest ablation across popular, medium and rare; v1 is also retained. |
| `student_graph_only_shared_category_nyc_20261016_v1` | Shared-category Graph-only construction, full object associations and acceptance evidence. |
| `student_graph_only_shared_category_nyc_medium1000_20261016_v1` | Original full medium1000 Graph-only timing adapter and reference-cache checks. |
| `student_exp5_ef_rerun_20261017_v1` | Original-category ef remeasurement on the existing three indexes, separate from α64. |
| `student_mars_freq_a2_v1` | Specialized α2 implementation used by that historical parameter point. |
| `student_mars_freq_a32_formal_20261015_v1` | Earlier α32 formal campaign. |
| `student_mars_freq_general_*`, `student_mars_NYC_alpha*`, and related pilot directories | Threshold trials, acceptance records and individual α results reused by the final campaign. |

The main α64 directory contains `run_campaign.py`, `run_extensions.py`, `export_final.py`, and their v2 execution adapters. `graph_only/rep2` and `graph_only/rep3` contain separate build and query trees (`runtime` and `runtime_query`), `run_shared.py`, `run_sweep.py`, and input/environment bindings. The final Graph-only tables also identify the reused first instance. Use the table's `raw_file`, `raw_sha256`, `index_binding` or `binding_file` fields to follow a particular round; do not select a directory solely because its name contains “final”.

Historical Python launchers are preserved evidence, including failed attempts and later fixes. They contain original paths, resume conditions, and CPU96/NUMA4 assumptions. For a fresh rerun, copy the required source/configuration to a new output tree, bind paths to the downloaded data, and record path or affinity changes separately. Do not execute a campaign coordinator inside its preserved result directory. The portable main wrapper performs this separation already; it does not silently reinterpret every historical campaign.

For Graph-only, `scripts/graph_only.py` prepares this separation automatically. It copies the accepted shared-category build/query sources without changing them, relocates the recorded compiler arguments, and keeps new outputs in a fresh directory:

```bash
python3 scripts/graph_only.py prepare --workspace /path/to/artifacts/MARSExp2 --output /path/to/new-graph-run
python3 scripts/graph_only.py compile --workspace /path/to/artifacts/MARSExp2 --output /path/to/new-graph-run
# Choose an available CPU and its NUMA node on your machine.
python3 scripts/graph_only.py stats --workspace /path/to/artifacts/MARSExp2 --output /path/to/new-graph-run --cpu 96 --numa 4
python3 scripts/graph_only.py build --workspace /path/to/artifacts/MARSExp2 --output /path/to/new-graph-run --cpu 96 --numa 4
python3 scripts/graph_only.py reload --workspace /path/to/artifacts/MARSExp2 --output /path/to/new-graph-run --cpu 96 --numa 4
python3 scripts/graph_only.py perf --workspace /path/to/artifacts/MARSExp2 --output /path/to/new-graph-run --cpu 96 --numa 4 --ef 1600
```

The build and reload stages retain the source's 100-query acceptance checks; performance uses the original medium1000 list and the formal reference-cache comparison. Repeat `perf` with ef 3200 and 6400 on the same new index. Use separate fresh directories for independently constructed instances. This helper records CPU/NUMA choices but does not reproduce the historical process-monitoring wrapper or claim the machine is isolated. The publication checked preparation and compilation; it did not execute another full Graph-only build.

## What the inventory means

`source_inventory.json` describes 45,836 regular files observed in the server workspace. Of these, 42,994 files are stored in `research-records.tar.gz`, with individual SHA-256 hashes, sizes, source paths and recorded modification times. The archive retains old results and diagnostics; it does not represent them all as scientifically accepted results. `symlinks.json` records file-symlink targets rather than restoring absolute links into the original server. Dataset files are separately hashed in `provenance/dataset_files.json`.

Generated `.bin` and `.hnsw` files, compiled executables and object libraries, Python bytecode, nested delivery archives and model weights are excluded. The disposition of each observed regular file is explicit in the inventory. Prepared embeddings are included, so model weights are unnecessary for rerunning these frozen inputs. The original embedding generator remains in the research archive; regenerating embeddings is a different experiment from using the supplied vectors.

Large index files have their paths and sizes inventoried. Existing index-hash manifests and binding files remain in the archived campaign directories. The packaging process did not reread and newly certify every excluded index byte. A preserved hash is a historical binding, not evidence that an excluded binary can be downloaded from this release. Keep this distinction when describing exact replay versus rebuilding an approximate index.

The local-history archive adds 9,504 files from local experiment outputs, Graph-only deliveries, plotting files, sample inputs, and the earlier October 15 publication checkout. `local_files.json` gives their hashes and archive-relative locations. Bundled gnuplot executables, archive duplicates, bytecode and generated binary indexes are excluded. Paper draft PDFs and unrelated presentation documents at the workspace root are not part of the experimental artifact.

## Freeze and verification

`provenance/FROZEN_SOURCE_HASHES.json` maps the frozen sources to the portable sources. `scripts/verify_sources.py` checks all 787 mapped files. Final per-query CSVs are checked by `scripts/verify_raw_results.py`, and `scripts/verify_results.py` recomputes summary statistics. Dataset verification compares actual input bytes with existing experiment bindings. The release manifest separately verifies transport integrity of compressed attachments.

After downloading the history, individual archive files can be checked with `python3 scripts/verify_archive.py --root /path/to/artifacts/MARSExp2 --inventory /path/to/artifacts/source_inventory.json`. Use the dataset or local-history inventory with its corresponding extraction root for the same check.

Original absolute paths identify provenance; they are not installation instructions. Logs, failure markers and superseded launchers explain how accepted measurements were reached. Read completion and audit records together with the raw binding rather than treating the presence of one old `STOPPED_ERROR.json` as the final campaign state.
