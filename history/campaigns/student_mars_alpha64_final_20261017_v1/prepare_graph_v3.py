from pathlib import Path
import json,shutil,hashlib,subprocess
P=Path(__file__).resolve().parent;S=P.parent;G=S/'student_graph_only_shared_category_nyc_20261016_v1';W=S/'student_graph_only_shared_category_nyc_ef3200_6400_20261016_v1'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,x):p.write_text(json.dumps(x,indent=2)+'\n')
assert len(json.loads((P/'configs/parameter_reused.json').read_text()))==14
for rep in [2,3]:
 D=P/f'graph_only/rep{rep}'
 if not D.exists():
  D.mkdir(parents=True)
  for sub in ['configs','logs','provenance','results','index']:(D/sub).mkdir()
  shutil.copytree(G/'runtime',D/'runtime',ignore=shutil.ignore_patterns('shared_graph'))
  shutil.copytree(G/'queries',D/'queries');shutil.copytree(W/'reference_cache',D/'reference_cache');shutil.copy2(W/'queries/NYC_medium1000.csv',D/'queries/NYC_medium1000.csv')
  for n in ['shared_build_progress.hpp','run_shared.py']:shutil.copy2(G/n,D/n)
  for n in ['actual_configuration.json','gt_path.txt']:shutil.copy2(G/'configs'/n,D/'configs'/n)
  for n in ['input_hashes.json','exact_road_units.tsv']:shutil.copy2(G/'provenance'/n,D/'provenance'/n)
  env=json.loads((G/'configs/environment.json').read_text());save(D/'configs/environment.json',{k:v.replace(str(G),str(D)) for k,v in env.items()});(D/'configs/frozen_index_root.txt').write_text(str(D/'index'))
  save(D/'configs/BUILD_AUTHORIZED_AFTER_SCALE.json',{'user_authorized_three_independent_instances':True})
  cmd=[x.replace(str(G),str(D)) for x in json.loads((G/'configs/compile_command.json').read_text())]
  with (D/'logs/compile_shared_graph.log').open('x') as f:r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
  assert r.returncode==0;save(D/'configs/compile_shared_graph.json',{'argv':cmd,'sha256':sha(D/'runtime/shared_graph')})
 # Query builds need the reader's cache-inspection methods.
 shutil.copytree(W/'runtime',D/'runtime_query',ignore=shutil.ignore_patterns('measure_once'))
 driver=D/'runtime_query/drivers/measure_once.cpp';driver.chmod(0o644);old=driver.read_text();new=old.replace('if(ef!="3200"&&ef!="6400")','if(ef!="1600"&&ef!="3200"&&ef!="6400")').replace('_medium,1,NYC,6000','_medium,'+str(rep)+',NYC,6000');assert new!=old;driver.write_text(new)
 oldrun=(W/'run_sweep.py').read_text();newrun=oldrun.replace("P/'runtime/measure_once'","P/'runtime_query/measure_once'");assert newrun!=oldrun;(D/'run_sweep.py').write_text(newrun)
 cmd=[x.replace(str(W)+'/runtime',str(D)+'/runtime_query').replace(str(W),str(D)) for x in json.loads((W/'configs/compile_command.json').read_text())]
 with (D/'logs/compile_query_v3.log').open('x') as f:r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
 assert r.returncode==0;save(D/'configs/compile_query_v3.json',{'argv':cmd,'sha256':sha(D/'runtime_query/measure_once')})
 for ef in [1600,3200,6400]:(D/f'results/ef{ef}').mkdir(exist_ok=True)
 for d,source in [('runtime',G),('runtime_query',W)]:
  for f in (D/d/'src_tree').rglob('*'):
   if f.is_file():assert sha(f)==sha(source/'runtime/src_tree'/f.relative_to(D/d/'src_tree'))
 save(D/'provenance/adapter_v3.json',{'build_source_equal_original':True,'query_source_equal_formal_sweep':True,'query_driver_changes_only':['allow ef1600','correct repetition label'],'runner_change_only':'new query binary path','failed_compile_logs_retained':True,'no_algorithm_or_cache_rule_change':True});print('GRAPH_PREPARED',rep,flush=True)
save(P/'provenance/EXTENSION_PREFLIGHT_PASS.json',{'parameter_existing_rounds':14,'parameter_new_rounds':16,'graph_existing_rounds':3,'graph_new_rounds':6,'graph_new_instances':2,'new_parameter_indices':16})
