from pathlib import Path
import os,sys,json,csv,subprocess,time,hashlib,statistics as st,math,traceback,datetime,fcntl
P=Path(__file__).resolve().parent;ENV=json.loads((P/'configs/environment.json').read_text());QUEUE=json.loads((P/'configs/queue.json').read_text());ROUNDS=[];GT_DONE=[]
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def save(p,x):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,indent=2)+'\n')
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def rows(p,sep=','):return list(csv.DictReader(Path(p).open(),delimiter=sep))
def status(s):save(P/'outputs/progress.json',{'utc':now(),'current':s,'formal_done':len(ROUNDS),'formal_planned':135,'GT_new_done':len(GT_DONE),'GT_new_planned':6,'CPU':96,'NUMA':4,'new_indices':0});print(s,flush=True)
def busy(exclude=()):
 result=[]
 for l in subprocess.check_output(['ps','-eLo','pid,lwp,psr,pcpu,stat,comm'],text=True).splitlines()[1:]:
  x=l.split()
  if len(x)>=6 and int(x[0]) not in exclude and int(x[2]) in [96,288] and float(x[3])>=1 and x[4].startswith('R'):result.append(l)
 return result
def run(label,args,region):
 status(label);assert not busy([os.getpid()]),'CPU96 or SMT sibling288 occupied'
 fields={x.split()[2].rstrip(':'):int(x.split()[3])*1024 for x in Path('/sys/devices/system/node/node4/meminfo').read_text().splitlines()};reclaim=fields['MemFree']+fields['Active(file)']+fields['Inactive(file)']+fields['SReclaimable']-fields['Dirty']-fields['Writeback'];assert reclaim>45*2**30,('insufficient local-node memory',reclaim)
 env={**ENV,'EXACT_ROAD_REGION':region,'EXACT_ROAD_ASSERTION_LOG':str(P/f'logs/{label}_exact_road.tsv')};cmd=['numactl','--all','--physcpubind=96','--membind=4']+list(map(str,args));violations=[];samples=0;peak=0
 with (P/f'logs/{label}.stdout.log').open('x') as out,(P/f'logs/{label}.stderr.log').open('x') as err,(P/f'logs/{label}.resources.jsonl').open('x') as sample:
  child=subprocess.Popen(cmd,cwd=P,env=env,stdin=subprocess.DEVNULL,stdout=out,stderr=err);save(P/f'logs/{label}.start.json',{'utc':now(),'pid':child.pid,'argv':cmd,'env':env,'reclaimable_node4_bytes':reclaim})
  while child.poll() is None:
   try:
    proc=Path(f'/proc/{child.pid}');exe=os.readlink(proc/'exe')
    if exe==str(args[0]):
     state={a:b.strip() for a,b in (l.split(':',1) for l in (proc/'status').read_text().splitlines() if ':' in l)};aff=sorted(os.sched_getaffinity(child.pid));threads=int(state['Threads']);peak=max(peak,int(state.get('VmHWM','0 kB').split()[0]));other=busy([child.pid,os.getpid()]);row={'utc':now(),'affinity':aff,'threads':threads,'peak_RSS_KiB':peak,'other_active_threads':other};samples+=1
     if aff!=[96] or threads!=1 or other:violations.append(row)
     if samples%5==0:
      for l in (proc/'numa_maps').read_text().splitlines():
       parts=l.split();kv=dict(t.split('=',1) for t in parts if '=' in t)
       if int(kv.get('anon','0'))>=1024 and (parts[1]!='bind:4' or any(k.startswith('N') and k[1:].isdigit() and k!='N4' and int(v)>0 for k,v in kv.items())):violations.append({'memory_binding':l})
     sample.write(json.dumps(row)+'\n');sample.flush()
   except (FileNotFoundError,ProcessLookupError):pass
   time.sleep(1)
  rc=child.wait()
 save(P/f'logs/{label}.exit.json',{'utc':now(),'exit_code':rc,'samples':samples,'violations':violations,'observed_peak_including_setup_and_queries_KiB':peak,'not_a_build_memory_measurement':True});assert rc==0,(label,rc);assert not violations,(label,'CPU/NUMA/thread conflict')
def gt_stage(reg):
 tag='q1000_seed42_natural_medium';out=P/f'gt_validation/{reg}/medium_fixed_queries.tsv'
 run(f'GT_reference_check_{reg}',[P/'bin/exact_gt_integer',reg,tag,5000,10,out,'0,1,63,845'],reg)
 old=rows(P/f'input_overlay/data/{reg}/groundtruth/{tag}_tau5000_k10.tsv','\t');new=rows(out,'\t');checks=[]
 for qid in ['0','1','63','845']:
  a=sorted([x for x in old if x['query_id']==qid],key=lambda x:int(x['rank']));b=sorted([x for x in new if x['query_id']==qid],key=lambda x:int(x['rank']));assert len(a)==len(b),(reg,qid,'GT count mismatch',len(a),len(b));assert all(abs(float(x['score'])-float(y['score']))<=1e-6 for x,y in zip(a,b)),(reg,qid,'GT score mismatch')
  checks.append({'query_id':qid,'GT_count':len(a),'score_sequence_equal_within_existing_semantic_tolerance':True,'object_IDs_equal':all(x['object_id']==y['object_id'] for x,y in zip(a,b))})
 save(P/f'gt_validation/{reg}/REFERENCE_PASS.json',{'region':reg,'checks':checks,'strict_integer_road_bound':True,'existing_GT_unchanged':True})
 for group in ['popular','rare']:
  tag=f'q1000_seed42_natural_{group}';out=P/f'groundtruth/{reg}/{tag}_tau5000_k10.tsv';assert not out.exists();run(f'GT_{reg}_{group}',[P/'bin/exact_gt_integer',reg,tag,5000,10,out],reg)
  rr=rows(out,'\t');manifest=rows(P/f'queries/{reg}_{group}.csv');qids={r['query_id'] for r in manifest};assert all(r['query_id'] in qids for r in rr)
  for qid in qids:
   r=[x for x in rr if x['query_id']==qid];assert len(r)<=10 and len({x['object_id'] for x in r})==len(r) and {int(x['rank']) for x in r}==set(range(1,len(r)+1));assert all(math.isfinite(float(x['score'])) for x in r)
  record={'region':reg,'group':group,'path':str(out),'sha256':sha(out),'query_count':1000,'queries_with_GT':len({r['query_id'] for r in rr}),'objects':len(rr)};GT_DONE.append(record);save(P/'outputs/generated_GT_bindings.json',GT_DONE)
def binding(method,reg,rep):
 b=json.loads((P/f'bindings/{method}_{reg}_rep{rep}.json').read_text());root=Path(b['instance'])
 for rel,info in b['files'].items():
  s=(root/rel).stat();assert s.st_size==info['bytes'] and s.st_mtime_ns==info['mtime_ns'],('index file changed',method,reg,rep,rel)
 return root
def validate(raw,qpath,job):
 rr=rows(raw);qq=rows(qpath);assert len(rr)==len(qq);method=job['method']
 for r,q in zip(rr,qq):
  assert all(r[k]==q[k] for k in ['query_id','row_index','source_vertex']);assert int(r['tau_m'])==5000 and r['k']=='10';ids=r['result_ids'].split(';') if r['result_ids'] else [];scores=r['exact_scores'].split(';') if r['exact_scores'] else []
  count=int(r['GT_result_count'] if method=='MARS' else r['GT_count']);assert int(r['recall_denominator'])==min(10,count);assert len(ids)==len(set(ids))==len(scores)<=min(10,count)
  rec=float(r['score_recall'] if method=='MARS' else r['Recall']);assert 0<=rec<=1 and all(math.isfinite(float(x)) for x in scores);assert math.isfinite(float(r['core_query_ms'])) and float(r['core_query_ms'])>0
  if method=='MARS':assert r['alpha']=='64' and r['rest_ef']=='1600' and r['variant']=='full' and int(r['extra_assertion_h2h'])==len(ids)
  else:
   assert r['reference_equal']=='1' and r['method']==method
   if method=='RoadScan':assert r['score_mode']=='estimate_only' and r['shared_score_reuse']=='OFF'
   else:assert all(int(r[k])==job['budget'] for k in ['ef_actual','candidate_budget','max_candidate_budget','final_budget'])
 return {'method':method,'region':job['region'],'group':job['group'],'rep':job['rep'],'tau_m':5000,'k':10,'queries':len(rr),'QPS':1000/st.mean(float(x['core_query_ms']) for x in rr),'Recall':st.mean(float(x['score_recall'] if method=='MARS' else x['Recall']) for x in rr),'empty_results':sum(not x['result_ids'] for x in rr),'under_k_returns':sum(len(x['result_ids'].split(';'))<10 if x['result_ids'] else True for x in rr),'raw_file':str(raw),'raw_sha256':sha(raw),'index_binding':str(P/f"bindings/{method}_{job['region']}_rep{job['rep']}.json"),'GT':job['GT'],'GT_sha256':sha(job['GT']),'CPU':96,'NUMA':4}
def measure(job,accept=False):
 method,reg,g,rep=[job[k] for k in ['method','region','group','rep']];root=binding(method,reg,rep);label=f"{'accept' if accept else 'formal'}_{method}_{reg}_{g}_rep{rep}";q=P/f"queries/{'accept_' if accept else ''}{reg}_{g}.csv"
 if method=='MARS':
  out=P/'mars_runtime'/('acceptance' if accept else 'runs')/label;task=P/f"configs/{'accept_' if accept else ''}{method}_{reg}_{g}.tsv";args=[P/'mars_runtime/bin/freq_perf','perf',reg,root,out,task,64];raw=out/'raw'/(job['task_id']+'.csv')
 else:
  out=P/('acceptance' if accept else 'runs')/label;task=P/f'configs/{method}_{reg}_{g}.tsv';seed=(root/'state_kind.txt').read_text().split()[2];args=[P/'bin'/('hnsw_groups' if method=='HNSW' else 'baseline_groups'),'accept' if accept else 'load',method,reg,root,out,task,q,seed,'reuse_off'];raw=out/(job['task_id']+'.csv')
  if method=='Mesh-Road':args.append(P/f'configs/Mesh_{reg}_placement.json')
 assert not out.exists();run(label,args,reg);binding(method,reg,rep);result=validate(raw,q,job)
 if method!='MARS':
  a=json.loads((out/'prepared_cache_state.json').read_text());b=json.loads((out/'after_queries_cache_state.json').read_text());assert a==b and a['name_capacity']==a['category_capacity']==200000
  for kind in ['name','category']:assert sha(out/f'prepared_{kind}_ids.txt')==sha(out/f'after_queries_{kind}_ids.txt')
  prep=json.loads((out/'vector_cache_preparation.json').read_text());assert not prep['uses_evaluation_queries_or_GT_for_selection'] and not prep['query_timer_includes_preparation']
 save(out/'VALIDATED.json',result)
 if not accept:
  assert result['queries']==1000;ROUNDS.append(result);save(P/'outputs/per_round.json',ROUNDS)
 status('VALIDATED_'+label)
def main():
 lock=(P/'serial.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB);assert not (P/'STARTED.json').exists();freeze=json.loads((P/'EXPERIMENT_FREEZE.json').read_text())
 for rel,h in freeze['files_sha256'].items():assert sha(P/rel)==h
 save(P/'STARTED.json',{'utc':now(),'pid':os.getpid(),'CPU':96,'NUMA':4})
 for reg in ['BAY','COL','FLA']:
  gt_stage(reg)
  for method in ['MARS','RoadScan','HNSW','SymphonyQG','Mesh-Road']:
   for g in ['popular','medium','rare']:measure(next(j for j in QUEUE if j['region']==reg and j['method']==method and j['group']==g and j['rep']==1),True)
  save(P/f'acceptance/{reg}_PASS.json',{'cases':60,'groups':3,'methods':5,'old_medium_GT_reference_passed':True})
  for rep in [1,2,3]:
   for method in ['MARS','RoadScan','HNSW','SymphonyQG','Mesh-Road']:
    for g in ['popular','medium','rare']:measure(next(j for j in QUEUE if j['region']==reg and j['method']==method and j['group']==g and j['rep']==rep))
 assert len(ROUNDS)==135;assert len({(r['region'],r['method'],r['group'],r['rep']) for r in ROUNDS})==135
 for rel,h in freeze['files_sha256'].items():assert sha(P/rel)==h
 save(P/'MEASUREMENTS_COMPLETE.json',{'utc':now(),'new_rounds':135,'new_indices':0,'new_GT':6});status('EXPORTING');subprocess.run([sys.executable,P/'export_results.py'],cwd=P,check=True);save(P/'COMPLETED.json',{'utc':now(),'formal_rounds':135,'all_four_region_rounds_with_NYC':180,'export_complete':True});status('COMPLETE')
if __name__=='__main__':
 try:main()
 except Exception as e:save(P/'STOPPED_ERROR.json',{'utc':now(),'error':str(e),'traceback':traceback.format_exc(),'all_records_preserved':True});print(traceback.format_exc(),flush=True);sys.exit(1)
