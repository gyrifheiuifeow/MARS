from pathlib import Path
import json,ast,hashlib,subprocess,datetime,csv,os
P=Path(__file__).resolve().parent
import formal_support as m
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert not (P/'FORMAL_FREEZE.json').exists() and not (P/'STARTED.json').exists()
assert (P/'provenance/PREFLIGHT_PASS.json').exists() and (P/'provenance/EXTENSION_PREFLIGHT_PASS.json').exists()
for name in ['run_campaign.py','formal_support.py','validate_partition.py','run_extensions.py','export_final.py']:ast.parse((P/name).read_text())
checked=[]
for r in json.loads((P/'configs/reused_performance.json').read_text()):
 m.bindings[r['region'],1]=json.loads((P/f"runs/{r['region']}_rep1/index_binding.json").read_text())
 v=m.validate(r['region'],1,'perf',r['config_id'],Path(r['raw_file']));assert v['raw_sha256']==r['raw_sha256'];checked.append(v)
assert len(checked)==5
from run_extensions import validate
q=P/'queries/NYC_medium.csv'
for r in json.loads((P/'configs/parameter_reused.json').read_text()):validate(r['local_copy'],q,r['alpha'],1600,'full')
for r in json.loads((P/'configs/graph_reused.json').read_text()):validate(r['raw_file'],q,None,r['ef'],'graph_shared_category')
# Check export keys and baseline configurations.
S=P.parent;old=S/'student_hnsw_empty_restart_candidate_20261008_v1/formal_runs/student_hnsw_formal_20261008_v1/outputs'
rr=list(csv.DictReader(next(old.glob('Exp1-3*完整*逐轮*.csv')).open(encoding='utf-8-sig')));assert len(rr)==540 and sum(x['method']!='MARS' for x in rr)==468
plan=json.loads((P/'configs/APPROVED_PLAN.json').read_text());assert plan['default_alpha']==64
files={}
for folder in ['src_tree','drivers','bin','configs','queries','graph_only']:
 for p in (P/folder).rglob('*'):
  if not p.is_file() or any(x in p.parts for x in ['logs','results','index','reference_cache']):continue
  files[str(p.relative_to(P))]=sha(p)
for name in ['run_campaign.py','formal_support.py','validate_partition.py','run_extensions.py','export_final.py']:files[name]=sha(P/name)
freeze={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'version':P.name,'default_alpha':64,'default_ef':1600,'files_sha256':files,'approved_plan':plan,'read_only_prelaunch_validator_pass':True,'query_algorithm_changes':False,'old_code_and_results_untouched':True,'source_instances_independent':True}
(P/'FORMAL_FREEZE.json').write_text(json.dumps(freeze,indent=2)+'\n')
env=json.loads((P/'configs/environment.json').read_text());f=(P/'coordinator.log').open('x');proc=subprocess.Popen(['/data/software/python-envs/ml-base/bin/python','-u',str(P/'run_campaign.py')],cwd=P,env=env,stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
(P/'LAUNCHED.json').write_text(json.dumps({'utc':freeze['utc'],'pid':proc.pid,'scope':'main alpha64 + existing alpha scan completion + Graph-only three-instance completion; no baseline reruns unless missing','hourly_monitor_requested':True},indent=2));print('CAMPAIGN_LAUNCHED',proc.pid,flush=True)
