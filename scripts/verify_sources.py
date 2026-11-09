#!/usr/bin/env python3
"""Check source checksums against the publication manifest."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[1]
records=json.loads((ROOT/"provenance/FROZEN_SOURCE_HASHES.json").read_text())
for r in records:
    assert hashlib.sha256((ROOT/r["path"]).read_bytes()).hexdigest()==r["published_sha256"],r["path"]
    if not r["changed_for_publication"]:assert r["published_sha256"]==r["frozen_sha256"]
print(f"Verified {len(records)} published source files and their frozen-source mapping.")
