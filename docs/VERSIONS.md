# Implementation versions

| Component | Version used by the saved results |
|---|---|
| MARS | October 17 α64 implementation, 144 performance runs and 48 diagnostic runs |
| HNSW | Empty-candidate restart version, followed by 132 remeasurement runs |
| Other baselines | Cache preparation and post-query integer-road validation |
| RoadScan | Score reuse off |
| Mesh-Road | Saved placement selection for each region |

The two runtime directories contain the versions used by MARS and the
baselines. The baseline directory has the HNSW retry patch and runs Mesh's extra
result check after the query timer.

`provenance/FROZEN_SOURCE_HASHES.json` stores the original and published hashes.
The publication entry points accept data, output and reference roots. They also
support loading an existing baseline instance from outside the output directory.
Comment edits have their own published hashes. CI compiles these entry points
and runs synthetic cases. The CSVs contain the recorded real-data measurements.

To reuse an index with `run_matrix.py --reuse-manifest`, provide its file hashes
and the directory containing its construction costs:

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

Replace the paths and hash placeholders with values for your instance. The
wrapper checks them before reuse. Rebuilding from a seed label can produce a
different approximate graph.
