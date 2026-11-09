from pathlib import Path
import json,csv,shutil,subprocess,hashlib
P=Path(__file__).resolve().parent;S=P.parent
def save(p,x):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,indent=2)+'\n')
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(8*1024*1024),b''):h.update(b)
 return h.hexdigest()
def rows(p):return list(csv.DictReader(Path(p).open()))
cid='NYC_t5000_full_ef1600_medium';refs=[]
for a,root in [(2,S/'student_mars_freq_a2_v1/execution_adapter_v2'),(32,S/'student_mars_freq_a32_formal_20261015_v1')]:
 rr=[r for r in rows(root/'outputs/performance_rounds.csv') if r['config_id']==cid];assert len(rr)==3
 for r in rr:
  assert sha(r['raw_file'])==r['raw_sha256'];bp=root/f"runs/NYC_rep{r['repetition']}/index_binding.json";assert bp.exists()
  refs.append({'alpha':a,'repetition':int(r['repetition']),'raw_file':r['raw_file'],'raw_sha256':r['raw_sha256'],'binding_file':str(bp),'source_version':'alpha2-specialized historical implementation' if a==2 else 'general threshold implementation','reused':True})
for root in [S/'student_mars_NYC_alpha2_256_full1000_20261016_v1',S/'student_mars_NYC_alpha512_full1000_20261017_v1',S/'student_mars_NYC_alpha1024_full1000_20261017_v1']:
 file=root/'outputs/new_results.json'
 data=json.loads((file if file.exists() else root/'outputs/result.json').read_text());rs=data if isinstance(data,list) else [data]
 for r in rs:
  a=int(r['alpha']);raw=Path(r['raw_file']);assert sha(raw)==r['raw_sha256'];bp=root/f'runs/a{a}/index_binding.json'
  if not bp.exists():
   choices=list(root.glob('runs/**/index_binding.json'));assert len(choices)==1;bp=choices[0]
  refs.append({'alpha':a,'repetition':1,'raw_file':str(raw),'raw_sha256':sha(raw),'binding_file':str(bp),'source_version':'general threshold implementation','reused':True})
assert len(refs)==14
manifest=rows(P/'queries/NYC_medium.csv')
for r in refs:
 rr=rows(r['raw_file']);assert len(rr)==1000
 for x,q in zip(rr,manifest):
  assert all(x[k]==q[k] for k in ['query_tag','query_id','row_index','source_vertex'])
  assert (x.get('alpha')==str(r['alpha']) or (r['alpha']==2 and 'alpha' not in x)) and x['tau_m']=='5000' and x['rest_ef']=='1600' and x['variant']=='full'
 b=json.loads(Path(r['binding_file']).read_text());root=Path(b['instance'])
 # Verify the reused index files against their saved hashes.
 for name,h in b['files'].items():assert (root/name).stat().st_size==h['bytes'] and sha(root/name)==h['sha256']
 dest=P/f"references/alpha_sweep/a{r['alpha']}_rep{r['repetition']}.csv";dest.parent.mkdir(exist_ok=True,parents=True);shutil.copy2(r['raw_file'],dest);r['local_copy']=str(dest)
 print('PARAMETER_REUSE',r['alpha'],r['repetition'],flush=True)
save(P/'configs/parameter_reused.json',refs)
G=S/'student_graph_only_shared_category_nyc_20261016_v1';M=S/'student_graph_only_shared_category_nyc_medium1000_20261016_v1';W=S/'student_graph_only_shared_category_nyc_ef3200_6400_20261016_v1'
gfreeze=json.loads((G/'SHARED_GRAPH_FREEZE.json').read_text());assert gfreeze['status']=='FROZEN_ACCEPTED'
for rel,x in gfreeze['index'].items():assert sha(G/rel)==x['sha256']
graphrefs=[]
for ef in [1600,3200,6400]:
 root=M if ef==1600 else W;raw=root/('results/NYC_graph_only_shared_medium1000.csv' if ef==1600 else f'results/ef{ef}/NYC_graph_only_shared_medium1000.csv')
 art=json.loads((root/'ARTIFACT_MANIFEST.json').read_text());assert sha(raw)==art[str(raw.relative_to(root))]['sha256'];assert len(rows(raw))==1000
 graphrefs.append({'ef':ef,'repetition':1,'raw_file':str(raw),'raw_sha256':sha(raw),'index_root':str(G/'index'),'binding_file':str(G/'SHARED_GRAPH_FREEZE.json'),'cpu':96,'numa':4,'reused':True})
save(P/'configs/graph_reused.json',graphrefs)
for rep in [2,3]:
 D=P/f'graph_only/rep{rep}';D.mkdir(parents=True)
 for sub in ['configs','logs','provenance','results','index']:(D/sub).mkdir()
 shutil.copytree(G/'runtime',D/'runtime',ignore=shutil.ignore_patterns('shared_graph'))
 for sub in ['queries']:shutil.copytree(G/sub,D/sub)
 for sub in ['reference_cache']:shutil.copytree(W/sub,D/sub)
 shutil.copy2(W/'queries/NYC_medium1000.csv',D/'queries/NYC_medium1000.csv')
 for f in (W/'runtime/drivers').iterdir():
  if not f.is_file():continue
  target=D/'runtime/drivers'/f.name
  if target.exists():assert sha(target)==sha(f)
  else:shutil.copy2(f,target)
 for n in ['shared_build_progress.hpp','run_shared.py']:shutil.copy2(G/n,D/n)
 shutil.copy2(W/'run_sweep.py',D/'run_sweep.py')
 for n in ['actual_configuration.json','gt_path.txt']:shutil.copy2(G/'configs'/n,D/'configs'/n)
 for n in ['input_hashes.json','exact_road_units.tsv']:shutil.copy2(G/'provenance'/n,D/'provenance'/n)
 env=json.loads((G/'configs/environment.json').read_text());env={k:v.replace(str(G),str(D)) for k,v in env.items()};save(D/'configs/environment.json',env)
 (D/'configs/frozen_index_root.txt').write_text(str(D/'index'));save(D/'configs/BUILD_AUTHORIZED_AFTER_SCALE.json',{'user_authorized_three_independent_instances':True})
 driver=D/'runtime/drivers/measure_once.cpp';driver.chmod(0o644);old=driver.read_text();new=old.replace('if(ef!="3200"&&ef!="6400")','if(ef!="1600"&&ef!="3200"&&ef!="6400")');assert new!=old;driver.write_text(new)
 save(D/'provenance/adapter_change.json',{'only_cli_allowlist_extended_to_existing_ef1600':True,'search_and_scoring_unchanged':True,'source_roots':[str(G),str(W)],'repetition':rep})
 for sub,exe in [(G,'shared_graph'),(W,'measure_once')]:
  cmd=[x.replace(str(sub),str(D)) for x in json.loads((sub/'configs/compile_command.json').read_text())]
  with (D/f'logs/compile_{exe}.log').open('x') as f:r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
  assert r.returncode==0;save(D/f'configs/compile_{exe}.json',{'argv':cmd,'sha256':sha(D/f'runtime/{exe}')})
 for ef in [1600,3200,6400]:(D/f'results/ef{ef}').mkdir()
 print('GRAPH_PREPARED',rep,flush=True)
save(P/'provenance/EXTENSION_PREFLIGHT_PASS.json',{'parameter_existing_rounds':14,'parameter_new_rounds':16,'graph_existing_rounds':3,'graph_new_rounds':6,'graph_new_instances':2,'new_parameter_indices':16,'default_main_counts_separate':True})
