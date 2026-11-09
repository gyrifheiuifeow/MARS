# Experiments

MARS uses α=64, k=10 and rest ef=1600 unless a task specifies another value.
It merges equal normalized vectors and counts their distinct owning objects.
Pivot holds vectors with frequency at least α. Each rest vector/object
association has a graph node. The `pivot` variant scores objects through pivot
postings with rest disabled.

| Experiment | Configuration |
|---|---|
| 1 | Four regions, 6 km, all five methods |
| 2 | Four regions, 2/4/6/8/10 km, including the shared 6 km configuration |
| 3 | Four regions, 3 km, candidate limits 128/512/1000/2000/4000/8000, with fixed MARS and RoadScan references |
| 4 | NYC medium, 5 km, rest ef 50/100/200/400/800/1600 |
| 5 | NYC popular/medium/rare, 5 km, full MARS and Pivot-only |
| 6 | NYC medium, 1/2/3/4/5 km, full MARS and Pivot-only |
| 7 | Four regions at 6 km and NYC at 2/4/8/10 km, full MARS and direct H2H checks |
| 8 | Index construction, saving, space and peak RSS |

After reusing shared configurations, the main MARS matrix has 48 performance
settings and 16 diagnostic settings. Each setting uses 1,000 queries on three
separately built index instances: 144 performance runs and 48 diagnostic runs.

The additional α study uses NYC medium at 5 km and ef=1600, with α values
2/4/8/16/32/64/96/128/256/512/1024. Graph-only uses NYC medium at 6 km and
ef=1600/3200/6400. Both studies have three instances per setting. The extended
query-group study covers all four regions at 5 km and all five methods.

For a new threshold run, pass `--alpha 128` to `scripts/reproduce.py run` and
choose a new output root. This wrapper uses the general implementation. The
saved α2 point came from an earlier specialized implementation and a different
measurement window.

## Baselines

HNSW uses M=16 and construction ef=100. SymphonyQG uses degree=32 and
construction ef=200. Their query ef is the larger of the configured ef and the
candidate limit. The task TSVs contain these settings. Both methods call the
road-feasibility predicate while collecting graph candidates.

Experiments 1 and 2 set both the initial and maximum candidate limits to 3000.
Experiment 3 sets both limits to the selected value in its sweep.

The HNSW runner makes one additional level-0 search from the saved global entry
if the first filtered vector-candidate set is empty. It skips the upper-layer
descent on this retry and keeps the same road callback and candidate limit.
The retry contributes to core query time. Some queries can still return an
empty approximate result.

Mesh-Road uses the saved placement selection: 8 GB for NYC/BAY/FLA and 4 GB for
COL. A candidate limit sets ef, the candidate budget and the maximum candidates
for each local index.

RoadScan uses QScan `estimate_only`, 5-bit quantization, score reuse off, and
the maximum aspect score for each object. Its quantized scores can miss the
exact score threshold used by Recall.

The baseline records label instances with seeds 100/101/102. RoadScan uses 0,
and the MARS constructions use label 100. Rebuilt graphs can differ from the
saved instances.

## Measurements

Core timing includes per-query initialization, search, road processing, object
score aggregation, result generation and cleanup. Evaluation and extra
correctness checks run after the timer stops. Cache preparation runs before
queries, in data order, with a capacity of 200,000 vectors per reader. Mesh's
extra validation runs outside the timer, while the HNSW retry runs inside it.

QPS is the query count divided by the sum of core query times. Recall uses the
score-threshold rule with tolerance 1e-6 and denominator `min(k, actual GT
count)`. The default BAY/COL workloads include queries with denominators 9 and
5. The score tolerance applies to semantic scores. Road feasibility uses the
distance budget without an added tolerance.

If H2H does not confirm a post-query road check, the assertion helper runs
Dijkstra on the original integer edge weights. Search itself continues to use
the H2H implementation.

The rest graph uses approximate beam search. Its road callback and theta filter
select output candidates while graph expansion follows the beam's convergence.

Graph-only ran on CPU96/NUMA4. The main MARS campaign used a single thread
without a fixed-core binding. The extra BAY/COL/FLA query-group runs used
CPU96/NUMA4, while NYC reused the earlier records. Ten windows among the 36
historical NYC baseline runs recorded other active processes. The logs do not
contain a separate measurement of their effect on QPS.

## Construction costs

The raw records split shared input loading, method preprocessing, structure
building, saving and reloading. MARS preprocessing includes vector merging,
frequency counting, channel assignment and association maps. RoadScan records
quantization and state-saving costs.

Peak build RSS includes shared inputs and saving. The measurement ends before
extra reload and validation queries. `serialized_bytes_total` counts bytes
written to index files. Structural space subtracts explicit copies of shared
vector payloads and includes the frequency and association maps.

Performance summaries use the performance runs. Phase diagnostics have their
own measurements. The tables report the available instance count, mean and
sample standard deviation for each configuration.
