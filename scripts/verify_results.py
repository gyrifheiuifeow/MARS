#!/usr/bin/env python3
"""Recompute summary statistics from per-instance results."""
from pathlib import Path
import csv, math, statistics, json
ROOT=Path(__file__).resolve().parents[1]
def rows(p):
 with p.open(encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f))
def verify(summary, rounds):
 ss,rr=rows(summary),rows(rounds);checks=0
 if summary.name=='all_four_regions_summary.csv':
  for r in rr:
   r['query_group']=r['group'];r['queries_per_instance']=r['queries']
   r['alpha']='64' if r['method']=='MARS' else ''
   r['rest_ef']='1600' if r['method']=='MARS' else ''
   r['Recall_percent']=str(float(r['Recall'])*100)
 for s in ss:
  keys=[k for k in s if k!='instances' and not k.endswith(('_mean','_sample_sd'))]
  group=[r for r in rr if all(r.get(k)==s[k] for k in keys)]
  assert len(group)==int(s['instances']),(summary.name,keys,s,len(group))
  for k,v in s.items():
   if not k.endswith(('_mean','_sample_sd')):continue
   base=k.removesuffix('_mean').removesuffix('_sample_sd')
   if base not in group[0]:raise AssertionError((summary.name,base))
   values=[float(r[base]) for r in group]
   got=statistics.stdev(values) if k.endswith('_sample_sd') else statistics.mean(values)
   assert math.isclose(float(v),got,rel_tol=1e-9,abs_tol=1e-8),(summary.name,k,v,got)
   checks+=1
 return checks
def main():
 total=0;pairs=[]
 for directory in [ROOT/'results/alpha64',ROOT/'results/query_groups_four_regions']:
  for s in sorted(directory.glob('*summary.csv')):
   r=s.with_name(s.name.replace('summary.csv','rounds.csv'))
   if r.exists():total+=verify(s,r);pairs.append(s.name)
 # Check all three repetitions for each configuration.
 records=rows(ROOT/'results/alpha64/MARS64_performance_rounds.csv')
 assert len(records)==144 and all(r['alpha']=='64' and r['queries']=='1000' for r in records)
 for config in {r['config_id'] for r in records}:
  assert {int(r['repetition']) for r in records if r['config_id']==config}=={1,2,3}
 print(json.dumps({'summary_tables':len(pairs),'numeric_checks':total,'alpha64_performance_rounds':len(records),'status':'PASS'},indent=2))
if __name__=='__main__':main()
