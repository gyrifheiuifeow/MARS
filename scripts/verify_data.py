#!/usr/bin/env python3
"""Check dataset, query and ground-truth checksums."""
from pathlib import Path
import argparse,hashlib,json
ROOT=Path(__file__).resolve().parents[1]

def check(data):
    hashes={}
    for record in json.loads((ROOT/"configs/dataset_bindings.json").read_text()).values():hashes.update(record["data_sha256"])
    hashes.update(json.loads((ROOT/"configs/query_gt_hashes.json").read_text()))
    for relative,expected in hashes.items():
        path=data/relative
        if not path.is_file() and "groundtruth/" in relative:
            parts=Path(relative).parts
            region=parts[1] if parts[0]=="data" else parts[parts.index("groundtruth")+1]
            path=data/"data"/region/"groundtruth"/parts[-1]
        if not path.is_file():raise RuntimeError(f"Missing frozen input: {relative}")
        h=hashlib.sha256()
        with path.open("rb") as f:
            for chunk in iter(lambda:f.read(8*1024*1024),b""):h.update(chunk)
        if h.hexdigest()!=expected:raise RuntimeError(f"Input hash mismatch: {relative}")
    print(f"Verified {len(hashes)} frozen inputs.")

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--data-root",type=Path,required=True);a=p.parse_args();check(a.data_root.resolve())
