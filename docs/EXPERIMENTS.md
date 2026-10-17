# Experiment protocol

Default: alpha=64, k=10, rest ef=1600. Frequency counts distinct owning objects
after exact normalized-vector merging. Names and categories share that identity.
Each rest (UID, object) association is a graph node. Pivot-only disables rest and
uses only the selected pivot scores.

| Experiment | Frozen settings |
|---|---|
| 1 | Four regions, 6 km, all methods |
| 2 | Four regions, 2/4/6/8/10 km; reuse the 6 km default |
| 3 | Four regions, 3 km; candidate limits 128/512/1000/2000/4000/8000; MARS/RoadScan fixed references |
| 4 | NYC 5 km, rest ef 50/100/200/400/800/1600 |
| 5 | NYC 5 km, original popular/medium/rare lists, full and Pivot-only |
| 6 | NYC 1/2/3/4/5 km, full and Pivot-only |
| 7 | Four regions at 6 km plus NYC 2/4/8/10 km, full and direct H2H |
| 8 | Build time, serialization, structural space and build peak RSS |

The October 17 extension adds an α sweep at 2/4/8/16/32/64/96/128/256/512/1024
on NYC medium, 5 km, ef=1600, with three instances per setting. Graph-only uses
NYC medium, 6 km, and ef=1600/3200/6400, also across three instances. The final
query-group extension covers all four regions at 5 km and all five methods.
These extensions are separate from the original 48-configuration main matrix.

For new general-threshold runs, `reproduce.py --alpha 128` binds the chosen
threshold into build, query and acceptance inputs; give each threshold a distinct
output root. This command uses the general implementation at every threshold.
The archived α2 measurement used its earlier specialized implementation, so a
new general α2 run must not be presented as exact replay of that historical row.

Reuse leaves 48 unique MARS performance settings. Each uses 1,000 queries on
three separately constructed instances. The 16 road-diagnostic variants also use
three instances. This gives 144 performance and 48 independent diagnostic runs.

## Baselines

- HNSW: M=16, build ef=100. Its configured search ef is stored in the TSV;
  actual ef is `max(configured ef, candidate budget)`.
- SymphonyQG: degree=32, build ef=200. Actual search ef also accounts for budget.
- Exp-1/2 explicitly fix both initial and maximum HNSW/SymphonyQG budgets to
  3000. Exp-3 sets both to the selected limit. No automatic extra expansion.
- These HNSW/SymphonyQG entries use the existing in-search road callback.
- HNSW performs at most one extra level-0 search from its saved global entry
  when the first filtered vector candidate set is empty. It skips another upper
  descent. The callback and budget remain unchanged. Fallback time is timed.
  Approximate empty results can remain; there is no retry-until-success loop.
- Mesh-Road retains frozen placement: 8 GB for NYC/BAY/FLA, 4 GB for COL.
  Its limit maps to ef, candidate budget and maximum candidates in each local
  index, not one global list.
- RoadScan uses QScan `estimate_only`, 5-bit quantization, score reuse OFF, and
  maximum aspect score per object. It does not use the old default compare probe.
- Baseline seed labels are 100/101/102 by instance; RoadScan uses 0 for
  deterministic preprocessing. MARS retains label 100 for each construction.
  These labels are not claimed to control every inherited random source.

## Query timing and quality

Normal initialization, search, road processing, object-score aggregation, result
generation and cleanup are timed. Recall and extra correctness assertions are
outside. Raw-vector cache preparation uses data order, keeps the 200,000-vector
capacity per reader, and happens outside the query timer. Mesh validation is
outside the timer; HNSW fallback is inside it.

Recall uses the existing score-threshold rule with semantic tolerance 1e-6 and
denominator min(k, actual GT count). The known default BAY/COL under-k queries
have denominators 9 and 5. Do not remove these queries or change the GT.

When H2H rejects an extra untimed road assertion, the assertion checks the
original integer edge weights independently. Distance must still be at most the
budget, without a road epsilon. This does not fix or claim to fix all internal
floating-point boundary behavior.

Rest graph search is approximate. Mapping/aggregation correctness does not imply
perfect Recall. Road predicates and theta are not described as controlling rest
neighbor expansion or stopping its traversal.

## Cost and reporting

Shared load, method-specific preprocessing, structure build, save and reload are
recorded separately. MARS preprocessing includes merging vectors, object-frequency
counting, splitting and required mappings. RoadScan has no ANN graph but has
quantization and state-saving costs.

Build peak RSS is captured before extra reload or validation queries and includes
shared input and saving. Complete serialized bytes differ from structural bytes
after excluding explicit raw-payload copies; the latter is not actual disk usage.
Frequency and association maps are included in structural space.

Performance and diagnostics run separately. Never derive performance QPS from
diagnostic time. Report the mean and sample standard deviation over three
instances. Keep incomplete coverage explicit; never select the fastest instance.
