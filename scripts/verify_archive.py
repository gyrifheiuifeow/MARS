#!/usr/bin/env python3
"""Verify an extracted server, dataset or local-history file inventory."""
from pathlib import Path
import argparse,hashlib,json
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True);p.add_argument('--inventory',type=Path,required=True);a=p.parse_args();root=a.root.resolve();count=0;skipped=0
 for record in json.loads(a.inventory.read_text(encoding='utf-8')):
  if record.get('disposition','records_asset')!='records_asset':skipped+=1;continue
  f=(root/record['path']).resolve()
  if not f.is_relative_to(root):raise RuntimeError('Inventory path escapes root')
  if not f.is_file() or f.stat().st_size!=record['bytes']:raise RuntimeError('Missing or wrong-sized file: '+record['path'])
  h=hashlib.sha256()
  with f.open('rb') as stream:
   for b in iter(lambda:stream.read(8*1024*1024),b''):h.update(b)
  if h.hexdigest()!=record['sha256']:raise RuntimeError('Hash mismatch: '+record['path'])
  count+=1
 print(json.dumps({'status':'PASS','verified_files':count,'excluded_inventory_entries':skipped},indent=2))
if __name__=='__main__':main()
