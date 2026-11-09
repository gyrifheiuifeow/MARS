# Experiment history

The dates below refer to the October 2026 experiment directories.

| Date | Work |
|---|---|
| October 5 | Original-category MARS measurements and the corrected rest and road experiments. |
| October 6 | Cache preparation moved outside core query timing in the measurement entry. |
| October 7 | The v3 entry added the post-query integer-road check and shared baseline timing protocol. |
| October 8 | HNSW added a bounded empty-candidate restart, followed by remeasurement. The main comparison and cost tables use these HNSW records. |
| October 9 | Original-category rest ablation on popular, medium and rare queries. |
| October 13 to 15 | Configuration, pivot-selection and query-origin checks, followed by the α32 campaign. |
| October 15 to 16 | Graph-only moved to shared category-ID nodes with full object associations. The medium1000 workload and ef sweeps used the formal cache and timing setup. |
| October 17 | The α64 campaign completed 144 performance runs, 48 diagnostic runs, twelve default index instances, 33 α-study records and nine Graph-only records. Its v2 adapter contains the continuation and export fixes. |
| October 17 | The four-region query-group table combined 45 NYC records with 135 new BAY/COL/FLA records. The separate category-based ef remeasurement kept its own results. |
| October 18 | Source trees, prepared data and saved CSVs were packaged with portable entry points. |

The α32, original-category and α64 directories retain their source and input
bindings. Graph-only has three index instances at each of three ef settings.
The earlier single-instance checks remain in their original directories.
