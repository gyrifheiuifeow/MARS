from pathlib import Path
import csv,json,shutil,hashlib,subprocess,os,difflib
R=Path('/workspace/MARSExp2');S=R/'submission_experiments_20260726/results/student_runs';P=S/'student_query_groups_three_regions_alpha64_20261017_v1';A=S/'student_mars_alpha64_final_20261017_v1';B=S/'student_exp5_baselines_query_groups_20261015_v2/execution_v4';V=S/'student_measure_entry_20261007_v3_exact_validation'
REG=['BAY','COL','FLA'];GROUPS=['popular','medium','rare'];METHODS=['MARS','RoadScan','HNSW','SymphonyQG','Mesh-Road']
def save(p,x):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,indent=2)+'\n')
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(8*1024*1024),b''):h.update(b)
 return h.hexdigest()
def write(p,rr,sep=','):
 with p.open('x',newline='') as f:w=csv.DictWriter(f,fieldnames=list(rr[0]),delimiter=sep,lineterminator='\n');w.writeheader();w.writerows(rr)
assert not (P/'configs').exists()
for d in ['bin','configs','provenance','queries','groundtruth','gt_validation','logs','outputs','runs','acceptance','bindings','input_overlay/data']:(P/d).mkdir(parents=True,exist_ok=True)
for d in ['baseline_runtime','hnsw_runtime','hnsw_aux']:shutil.copytree(B/d,P/d)
(P/'mars_runtime').mkdir()
for d in ['src_tree','drivers']:shutil.copytree(A/d,P/'mars_runtime'/d)
(P/'mars_runtime/bin').mkdir();(P/'mars_runtime/runs').mkdir();(P/'mars_runtime/acceptance').mkdir()
same={}
for source,dest in [(B/'baseline_runtime',P/'baseline_runtime'),(B/'hnsw_runtime',P/'hnsw_runtime'),(A/'src_tree',P/'mars_runtime/src_tree'),(A/'drivers',P/'mars_runtime/drivers')]:
 for f in source.rglob('*'):
  if f.is_file():h=sha(f);assert h==sha(dest/f.relative_to(source));same[str(dest.relative_to(P)/f.relative_to(source))]=h
save(P/'provenance/unchanged_search_source.json',same)
env=json.loads((B/'configs/environment.json').read_text());env.update(MARS_ROOT=str(P/'input_overlay'),STUDENT_FREQ_ALPHA='64',OMP_DYNAMIC='FALSE',MKL_DYNAMIC='FALSE');env.pop('EXACT_ROAD_REGION',None);env.pop('EXACT_ROAD_ASSERTION_LOG',None)
shutil.copy2(A/'configs/exact_road_units.tsv',P/'configs/exact_road_units.tsv');env['EXACT_ROAD_UNITS_MANIFEST']=str(P/'configs/exact_road_units.tsv');save(P/'configs/environment.json',env)
units=list(csv.DictReader((P/'configs/exact_road_units.tsv').open(),delimiter='\t'));assert all(int(x['units_per_metre'])==10 for x in units)
for item in R.iterdir():
 if item.name!='data':(P/'input_overlay'/item.name).symlink_to(item,target_is_directory=item.is_dir())
inputs=[];queue=[]
for reg in REG:
 data=R/'data'/reg;overlay=P/'input_overlay/data'/reg;overlay.mkdir();(overlay/'groundtruth').mkdir();(P/'groundtruth'/reg).mkdir();(P/'gt_validation'/reg).mkdir()
 for item in data.iterdir():
  if item.name!='groundtruth':(overlay/item.name).symlink_to(item,target_is_directory=item.is_dir())
 for item in (data/'groundtruth').iterdir():(overlay/'groundtruth'/item.name).symlink_to(item)
 for g in GROUPS:
  tag=f'q1000_seed42_natural_{g}';meta=data/'queries'/(tag+'.meta.tsv');fv=data/'queries'/(tag+'.fvecs');rr=list(csv.DictReader(meta.open(),delimiter='\t'));assert len(rr)==1000
  picks=[{'group':g,'query_tag':tag,'query_id':int(x['query_id']),'row_index':i,'source_vertex':int(x['source_vertex_id'])} for i,x in enumerate(rr)];assert len({x['query_id'] for x in picks})==1000
  write(P/f'queries/{reg}_{g}.csv',picks);write(P/f'queries/accept_{reg}_{g}.csv',[picks[i] for i in [0,1,63,845]])
  for f in [meta,fv,data/'queries'/(tag+'.manifest.json')]:inputs.append({'path':str(f),'bytes':f.stat().st_size,'mtime_ns':f.stat().st_mtime_ns,'sha256':sha(f)})
  gt=data/'groundtruth'/f'{tag}_tau5000_k10.tsv'
  if g=='medium':assert gt.exists()
  elif not gt.exists():
   gt=P/'groundtruth'/reg/f'{tag}_tau5000_k10.tsv';(overlay/'groundtruth'/gt.name).symlink_to(gt)
  else:raise RuntimeError('Unexpected matching GT now available; review reuse before regenerating')
  for method in METHODS:
   budget=0 if method in ['MARS','RoadScan'] else (128 if reg=='BAY' else 256) if method=='Mesh-Road' else 3000
   ef=1600 if method=='MARS' else budget if method=='Mesh-Road' else 100
   cid=f'{reg}_t5000_full_ef1600_{g}' if method=='MARS' else f'{method}_{reg}_t5000_{g}'
   if method=='MARS':task={'config_id':cid,'tau':5000,'k':10,'variant':'full','rest_ef':1600,'query_manifest':str(P/f'queries/{reg}_{g}.csv'),'workload':g,'alpha':64}
   else:task={'task_id':cid,'tau_m':5000,'k':10,'budget':budget,'ef_config':ef,'query_tag':tag,'GT_path':str(gt)}
   write(P/f'configs/{method}_{reg}_{g}.tsv',[task],'\t')
   if method=='MARS':task={**task,'query_manifest':str(P/f'queries/accept_{reg}_{g}.csv')};write(P/f'configs/accept_{method}_{reg}_{g}.tsv',[task],'\t')
   for rep in [1,2,3]:queue.append({'method':method,'region':reg,'group':g,'rep':rep,'task_id':cid,'tau_m':5000,'k':10,'queries':1000,'budget':budget,'ef':ef,'GT':str(gt)})
 for method in METHODS:
  for rep in [1,2,3]:
   src=A/f'runs/{reg}_rep{rep}/index_binding.json' if method=='MARS' else V/f'bindings/{method}_{reg}_rep{rep}.json';b=json.loads(src.read_text());instance=Path(b['instance']);assert instance.is_dir();current={}
   bound=b['files'] if method=='MARS' else b['files_sha256']
   for rel,rec in bound.items():
    f=instance/rel;assert f.is_file();h=rec['sha256'] if isinstance(rec,dict) else rec
    if isinstance(rec,dict):assert f.stat().st_size==rec['bytes']
    current[rel]={'bytes':f.stat().st_size,'mtime_ns':f.stat().st_mtime_ns,'sha256_from_prior_verified_binding':h}
   if method=='MARS':assert json.loads((instance/'identity.json').read_text())['alpha']==64
   else:assert (instance/'state_kind.txt').read_text().split()[:2]==[method,reg]
   save(P/f'bindings/{method}_{reg}_rep{rep}.json',{'method':method,'region':reg,'rep':rep,'instance':str(instance),'source_binding':str(src),'source_binding_sha256':sha(src),'files':current,'large_index_hashes_inherited_from_qualified_binding_not_recomputed':True})
  if method=='Mesh-Road':shutil.copy2(S/f'student_measure_entry_20261006_v2_cache/configs/v1/Mesh_{reg}_placement.json',P/f'configs/Mesh_{reg}_placement.json')
save(P/'configs/queue.json',queue);save(P/'provenance/query_input_bindings.json',inputs)
# Fresh exact GT for the six missing natural-query workloads. No old GT overwritten.
original=(R/'code/exact_groundtruth_h2h.cpp').read_text();t=original.replace('#include "h2h.h"','#include <climits>\n#include <cfloat>').replace('typedef pair<int, double> ID;','typedef pair<int, long long> ID;')
t=t.replace('w = static_cast<double>(intw) / 10.0;','w = intw; // frozen manifest: 10 integer units per metre')
t=t.replace('vector<double> dist(N, DBL_MAX);','const long long bound=llround(tau*10.0);\n    vector<long long> dist(N, LLONG_MAX);').replace('priority_queue<pair<double, int>, vector<pair<double, int> >, greater<pair<double, int> > > pq;','priority_queue<pair<long long, int>, vector<pair<long long, int> >, greater<pair<long long, int> > > pq;').replace('pair<double, int> cur = pq.top();','pair<long long, int> cur = pq.top();').replace('double d = cur.first;','long long d = cur.first;').replace('if (d > tau)','if (d > bound)').replace('double nd = d + e.second;','long long nd = d + e.second;').replace('if (nd <= tau && nd < dist[v])','if (nd <= bound && nd < dist[v])')
t=t.replace('    ofstream out(output);','    if (ifstream(output).good()) throw runtime_error("refuse overwrite GT");\n    ofstream out(output);')
t=t.replace('        priority_queue<ResultItem> heap;','        if (argc>6) { string ids=","+string(argv[6])+","; if(ids.find(","+to_string(query_meta[qi].query_id)+",")==string::npos) continue; }\n        priority_queue<ResultItem> heap;')
assert t!=original;(P/'groundtruth/exact_gt_integer.cpp').write_text(t);(P/'provenance/GT_generator.diff').write_text(''.join(difflib.unified_diff(original.splitlines(True),t.splitlines(True),fromfile='original_exact_GT',tofile='new_integer_road_GT')))
save(P/'provenance/GT_generation_policy.json',{'missing_GT_files':6,'reason':'existing non-natural tags use different query vector files','unchanged_semantic_normalization_dot_object_max_and_topk':True,'integer_road_distance_units_per_metre':10,'strict_distance_le_tau':True,'old_GT_never_overwritten':True,'validation_query_ids':[0,1,63,845],'reuse_existing_medium_GT':True})
commands=[]
for job in json.loads((B/'configs/compile_commands.json').read_text()):
 oldroot=str(S/'student_exp5_baselines_query_groups_20261015_v2');cmd=[x.replace(oldroot,str(P)) for x in job['args']];commands.append({'name':job['name'],'argv':cmd,'output':job['output'].replace(oldroot,str(P))})
basecmd=next(x for x in json.loads((A/'provenance/builds.json').read_text()) if x['mode']=='perf')['argv'];commands.append({'name':'mars_runtime','argv':[x.replace(str(A),str(P/'mars_runtime')) for x in basecmd],'output':str(P/'mars_runtime/bin/freq_perf')})
commands.append({'name':'exact_gt','argv':['g++','-std=c++17','-O3','-mcmodel=large',str(P/'groundtruth/exact_gt_integer.cpp'),'-o',str(P/'bin/exact_gt_integer')],'output':str(P/'bin/exact_gt_integer')})
for job in commands:
 print('COMPILE',job['name'],flush=True)
 with (P/f"logs/compile_{job['name']}.log").open('x') as f:r=subprocess.run(['numactl','--all','--physcpubind=96','--membind=4']+job['argv'],stdout=f,stderr=subprocess.STDOUT)
 assert r.returncode==0;job['sha256']=sha(job['output'])
save(P/'provenance/compiled_programs.json',commands)
plan={'regions':REG,'groups':GROUPS,'methods':METHODS,'tau_m':5000,'k':10,'MARS_alpha':64,'MARS_ef':1600,'formal_rounds':135,'new_indices':0,'existing_NYC_rounds_preserved':45,'combined_rounds':180,'CPU':96,'NUMA':4,'sibling_avoid':288,'serial':True,'GT_missing_to_generate':6,'old_GT_unchanged':True,'NYC_affinity_differs_disclose':True};save(P/'APPROVED_PLAN.json',plan);save(P/'outputs/progress.json',{'stage':'SETUP_COMPLETE','formal_done':0,'formal_planned':135,'GT_new_done':0,'GT_new_planned':6});print('SETUP_COMPLETE',flush=True)
