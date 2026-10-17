from pathlib import Path
import json,ast,hashlib,subprocess,datetime
P=Path(__file__).resolve().parent
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
assert (P/'APPROVED_PLAN.json').exists() and not (P/'EXPERIMENT_FREEZE.json').exists()
for f in ['run_queue.py','export_results.py']:ast.parse((P/f).read_text())
files={}
for d in ['configs','queries','bindings','baseline_runtime','hnsw_runtime','hnsw_aux','mars_runtime','bin']:
 for f in (P/d).rglob('*'):
  if f.is_file():files[str(f.relative_to(P))]=sha(f)
for f in ['run_queue.py','export_results.py','groundtruth/exact_gt_integer.cpp']:files[f]=sha(P/f)
(P/'EXPERIMENT_FREEZE.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'version':P.name,'files_sha256':files,'formal_rounds':135,'new_indices':0,'CPU':96,'NUMA':4,'GT_generation_is_preparation_not_query_performance':True},indent=2))
env=json.loads((P/'configs/environment.json').read_text());out=(P/'coordinator.log').open('x');proc=subprocess.Popen(['/data/software/python-envs/ml-base/bin/python','-u',str(P/'run_queue.py')],cwd=P,env=env,stdin=subprocess.DEVNULL,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
(P/'LAUNCHED.json').write_text(json.dumps({'pid':proc.pid,'CPU':96,'NUMA':4},indent=2));print('LAUNCHED',proc.pid)
