# Data layout

Set `--data-root` to the directory containing `data/NYC`, `data/BAY`, `data/COL`
and `data/FLA`. The runner passes this directory to C++ as `MARS_ROOT`.

```text
MARS-data/
  data/
    NYC/
      USA-road-d.NYC.gr
      USA-road-d.NYC.co
      order.txt
      poi_node_mapping_Ext3.txt
      poi.fvecs
      cat.fvecs
      categories_sorted.csv
      categories.txt
      POInames_ID.txt
      queries/
        q1000_seed42_natural_medium.fvecs
        q1000_seed42_natural_medium.meta.tsv
        q1000_seed42_natural_medium.manifest.json
        ...
      groundtruth/
        q1000_seed42_natural_medium_tau6000_k10.tsv
        ...
```

BAY, COL and FLA use the same layout with their own region code. Each region
has popular, medium and rare workloads. The older
`results_tau_all_2000_10000_20260707_000802/groundtruth/REGION` directory is also
supported. The path-preparation script prefers a matching file under
`data/REGION/groundtruth`.

Vectors have 768 coordinates in fvecs format. The supplied source vector IDs
define the mapping to objects. Object IDs and merged vector UIDs start at zero.
Input road vertices and query-CSV vertices start at one, with conversion at the
oracle interface. `configs/road_units.tsv` gives the graph units and checksums.

`configs/dataset_bindings.json` and `configs/query_gt_hashes.json` contain the
input hashes. The query CSVs specify rows and source vertices. Keep the matching
vector and GT files under the data root.

Before running real-data acceptance, construct an index and generate its
independent reference:

```bash
python3 scripts/reproduce.py run --method MARS --region NYC --rep 1 \
  --stage build --data-root /path/to/MARS-data --output-root /path/to/new-run
python3 scripts/reproduce.py run --method MARS --region NYC --rep 1 \
  --stage reference --data-root /path/to/MARS-data --output-root /path/to/new-run
python3 scripts/reproduce.py run --method MARS --region NYC --rep 1 \
  --stage accept --data-root /path/to/MARS-data --output-root /path/to/new-run
```

The matrix helper compares the first instance's partition with the independent
reference and saved frequency state before measurement. Allow storage for data,
indexes, references and outputs. The integer-road manifest parser splits fields
on whitespace, so the data-root path must have no spaces.
