# Prepared data

`--data-root` becomes the C++ `MARS_ROOT`. It contains the prepared data:

```text
MARS-data/
  data/
    NYC/                         # also BAY, COL, FLA
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
        ... popular and rare for all four regions
      groundtruth/
        q1000_seed42_natural_medium_tau6000_k10.tsv
        ... the other frozen settings
```

The legacy `results_tau_all_2000_10000_20260707_000802/groundtruth/REGION`
directory is also supported. A matching `data/REGION/groundtruth` file is preferred.
Do not generate replacement GT silently.

Vectors have dimension 768 and use the existing fvecs layout. Original source
vector IDs remain unchanged. Object IDs and merged UIDs are zero based. Input
road vertices and query CSV vertices are one based; the oracle adapter converts
them to zero based. Verified units and graph checksums are in `configs/road_units.tsv`.

`configs/dataset_bindings.json` and `configs/query_gt_hashes.json` record input
checksums. Checked-in query CSVs specify exact rows and vertices, not the vector
payload. Download matching embeddings and GT with `scripts/fetch_artifacts.py --kind data --output /path/to/artifacts`; the data root is `/path/to/artifacts/MARSExp2`. No API credentials
or automatic external embedding calls are provided.

For real-data acceptance, generate the independent canonical reference first:

```bash
python3 scripts/reproduce.py run --method MARS --region NYC --rep 1 \
  --stage reference --data-root /path/to/MARS-data --output-root /path/to/new-run
python3 scripts/reproduce.py run --method MARS --region NYC --rep 1 \
  --stage accept --data-root /path/to/MARS-data --output-root /path/to/new-run
```

The matrix helper compares a production partition to the independent reference
and the actual saved frequency state before measuring each region's first instance.
References and indexes are large. Keep sufficient storage for twelve instances,
references, data and outputs. Use a path without spaces for the data root because
the frozen integer-road manifest parser treats whitespace as field separation.
