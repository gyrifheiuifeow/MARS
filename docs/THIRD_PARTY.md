# Third-party components

The snapshots include modified headers from
[RaBitQ-Library](https://github.com/VectorDB-NTU/RaBitQ-Library), whose upstream
license is Apache-2.0. Its text is retained in `licenses/RaBitQ-Library-LICENSE`.

Eigen and hnswlib notices remain under each runtime's
`src_tree/baselines/RaBitQ-Library-main/include/rabitqlib/third/` directory.
Keep these notices when redistributing the dependencies. The stored snapshots,
not the latest upstream library release, define the experiments.

The provenance manifest records modified headers, including the final baseline
HNSW restart. Do not silently replace these files with newer upstream code when
comparing with frozen measurements.
