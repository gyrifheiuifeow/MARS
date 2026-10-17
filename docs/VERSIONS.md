# Final versions and publication adapters

| Component | Final version |
|---|---|
| MARS | October 17 frequency alpha64 formal implementation, 144 performance/48 diagnostic runs |
| HNSW | Accepted one-pass empty-candidate restart, used for the final 132-run remeasurement |
| Other baselines | Frozen cache-prepared entry with strict untimed integer-road verification |
| RoadScan reuse | OFF |
| Mesh placement | Existing frozen per-region selection |

The two runtime trees retain their corresponding snapshots. The baseline HNSW
is not taken from the older header bundled in the MARS snapshot. Baseline Mesh
includes the validated relocation of its correctness check outside the query timer.

`provenance/FROZEN_SOURCE_HASHES.json` maps frozen source hashes to published
hashes. Unchanged files match byte for byte. Publication changes are limited to
entry output-root handling, the acceptance reference-root path, and accepting
explicit read-only baseline instances outside the output directory. Search,
scores, thresholds, filters and query timing boundaries are retained.

Python scripts are portable publication adapters. They do not claim their CI
checks are another real-data experiment. Source compilation and synthetic tests
are distinct from real-data acceptance and formal measurement.

For index reuse, supply the original input/parameter binding, file hashes and
cost records. `run_matrix.py --reuse-manifest` accepts this format:

```json
[
  {
    "region": "NYC",
    "repetition": 1,
    "instance": "/path/to/qualified-alpha64-instance",
    "cost_records": "/path/to/original-cost-records",
    "files": {
      "frequency_state.bin": "replace-with-verified-sha256",
      "mars_aux.bin": "replace-with-verified-sha256",
      "rest_graph.bin": "replace-with-verified-sha256",
      "identity.json": "replace-with-verified-sha256"
    }
  }
]
```

This is a format example, not a ready-to-run binding. A seed label cannot replace
saved graph files. Fresh constructions can differ in bytes and approximate results.
