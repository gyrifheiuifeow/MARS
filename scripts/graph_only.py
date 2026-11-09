#!/usr/bin/env python3
"""Run the shared-category Graph-only experiment."""
from pathlib import Path
import argparse,csv,json,os,shutil,subprocess,hashlib
REPO=Path(__file__).resolve().parents[1]
CAMPAIGN='submission_experiments_20260726/results/student_runs/student_mars_alpha64_final_20261017_v1/graph_only/rep2'
def digest(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(8*1024*1024),b''):h.update(b)
 return h.hexdigest()
def prepare(workspace,out):
 source=workspace/CAMPAIGN
 if not source.is_dir():raise RuntimeError('Download the records archive first; missing '+str(source))
 if out.exists():raise RuntimeError('Preparation requires a new output directory: '+str(out))
 out.mkdir(parents=True)
 bindings=[]
 for name in ['runtime','runtime_query','queries','reference_cache']:
  for p in sorted((source/name).rglob('*')):
   if not p.is_file() or p.is_symlink() or p.suffix in {'.bin','.pyc'}:continue
   with p.open('rb') as f:head=f.read(4)
   if head==b'\x7fELF':continue
   dest=out/p.relative_to(source);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
   bindings.append({'path':dest.relative_to(out).as_posix(),'sha256':digest(dest)})
 shutil.copyfile(source/'shared_build_progress.hpp',out/'shared_build_progress.hpp')
 bindings.append({'path':'shared_build_progress.hpp','sha256':digest(out/'shared_build_progress.hpp')})
 for name in ['configs','results','index','logs','provenance']:(out/name).mkdir(exist_ok=True)
 for name in ['compile_shared_graph.json','compile_query_v3.json']:
  record=json.loads((source/'configs'/name).read_text())
  old=next(x for x in record['argv'] if x.endswith('/drivers/shared_graph.cpp') or x.endswith('/drivers/measure_once.cpp'))
  old=old.split('/runtime',1)[0]
  cmd=[x.replace(old,str(out)) for x in record['argv']]
  (out/'configs'/name).write_text(json.dumps({'argv':cmd,'original_argv':record['argv']},indent=2))
 gt=workspace/'results_tau_all_2000_10000_20260707_000802/groundtruth/NYC/q1000_seed42_natural_medium_tau6000_k10.tsv'
 if not gt.is_file():raise RuntimeError('Missing frozen GT: '+str(gt))
 (out/'configs/gt_path.txt').write_text(str(gt))
 (out/'configs/frozen_index_root.txt').write_text(str(out/'index'))
 with (REPO/'configs/road_units.tsv').open() as f:rows=list(csv.DictReader(f,delimiter='\t'))
 for row in rows:row['graph_path']=str(workspace/row['graph_path'])
 with (out/'provenance/exact_road_units.tsv').open('w',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(rows[0]),delimiter='\t');w.writeheader();w.writerows(rows)
 (out/'configs/publication.json').write_text(json.dumps({'workspace':str(workspace),'source':str(source),'source_bindings':bindings,'variant':'shared category ID graph; no frequency partition','fresh_run_not_historical_replay':True},indent=2))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('stage',choices=['prepare','compile','stats','build','reload','perf']);p.add_argument('--workspace',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--ef',type=int,choices=[1600,3200,6400],default=1600);p.add_argument('--cpu',type=int);p.add_argument('--numa',type=int);a=p.parse_args();out=a.output.resolve();workspace=a.workspace.resolve()
 if a.stage=='prepare':prepare(workspace,out);print('Prepared',out);return
 cfg=json.loads((out/'configs/publication.json').read_text())
 if cfg['workspace']!=str(workspace):raise RuntimeError('Workspace differs from prepared binding')
 for record in cfg['source_bindings']:
  if digest(out/record['path'])!=record['sha256']:raise RuntimeError('Prepared source changed: '+record['path'])
 if a.stage=='compile':
  for name in ['compile_shared_graph.json','compile_query_v3.json']:
   cmd=json.loads((out/'configs'/name).read_text())['argv']
   with (out/'logs'/(name+'.log')).open('x') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
  print('Compiled Graph-only construction and query programs. No data index built.');return
 if a.cpu is None or a.numa is None:p.error('Specify --cpu and --numa for your machine; historical runs used 96 and 4.')
 if a.cpu not in os.sched_getaffinity(0):p.error('Requested CPU is unavailable to this process.')
 gt=(out/'configs/gt_path.txt').read_text();env=dict(os.environ)
 env.update(MARS_ROOT=str(workspace),OMP_NUM_THREADS='1',OPENBLAS_NUM_THREADS='1',MKL_NUM_THREADS='1',MARS_CAT_SCORE_MODE='channel',MARS_CAT_PIVOT='0',MARS_CAT_UNIQUE='1',MARS_CAT_POINTWISE_ROAD='0',MARS_CAT_UNIQUE_EF=str(a.ef),EXACT_ROAD_UNITS_MANIFEST=str(out/'provenance/exact_road_units.tsv'),EXACT_ROAD_ASSERTION_LOG=str(out/'logs'/(a.stage+f'_ef{a.ef}_exact_road.tsv')))
 label=a.stage+(f'_ef{a.ef}' if a.stage=='perf' else '')
 cmd=['numactl',f'--physcpubind={a.cpu}',f'--membind={a.numa}']
 if a.stage=='perf':
  (out/'results'/f'ef{a.ef}').mkdir(exist_ok=False);cmd += [str(out/'runtime_query/measure_once'),gt,str(a.ef)]
 else:cmd += [str(out/'runtime/shared_graph'),a.stage,gt]
 with (out/'logs'/(label+'.command.json')).open('x') as f:json.dump({'argv':cmd,'cpu':a.cpu,'numa':a.numa,'environment':{k:env[k] for k in ['MARS_ROOT','OMP_NUM_THREADS','EXACT_ROAD_UNITS_MANIFEST']}},f,indent=2)
 with (out/'logs'/(label+'.log')).open('x') as f:subprocess.run(cmd,env=env,cwd=out,stdout=f,stderr=subprocess.STDOUT,check=True)
 print('Completed',label)
if __name__=='__main__':main()
