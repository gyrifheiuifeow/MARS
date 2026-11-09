#!/usr/bin/env python3
"""Check raw-query files and recompute QPS and Recall."""
from pathlib import Path
import csv,hashlib,statistics,math,json
ROOT=Path(__file__).resolve().parents[1]
def rows(p):
 with p.open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f))
def main():
 seen={};bindings=0;numeric=0
 for folder in [ROOT/'results/alpha64',ROOT/'results/query_groups_four_regions']:
  for table in folder.glob('*rounds.csv'):
   for r in rows(table):
    if not r.get('package_raw'):continue
    p=folder/r['package_raw'];assert p.is_file(),p
    if p not in seen:
     content=p.read_bytes();digest=hashlib.sha256(content).hexdigest();raw=rows(p)
     seen[p]=(digest,raw)
    digest,raw=seen[p];assert digest==r['raw_sha256'],(p,digest,r['raw_sha256'])
    if r.get('queries'):assert len(raw)==int(r['queries']),p
    bindings+=1
    if r.get('mode')=='diag' or not r.get('QPS'):continue
    qps=len(raw)*1000/sum(float(x['core_query_ms']) for x in raw)
    recall_key='score_recall' if 'score_recall' in raw[0] else 'Recall'
    recall=statistics.mean(float(x[recall_key]) for x in raw)
    assert math.isclose(qps,float(r['QPS']),rel_tol=1e-9,abs_tol=1e-8),(p,qps,r['QPS'])
    reference=r.get('Recall') or r.get('Recall_audit')
    if reference:assert math.isclose(recall,float(reference),rel_tol=1e-9,abs_tol=1e-8),(p,recall,reference)
    numeric+=1
 print(json.dumps({'unique_raw_files':len(seen),'verified_bindings':bindings,'recomputed_performance_rows':numeric,'status':'PASS'},indent=2))
if __name__=='__main__':main()
