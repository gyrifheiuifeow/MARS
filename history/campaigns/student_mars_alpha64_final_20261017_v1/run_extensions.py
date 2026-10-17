from pathlib import Path
import json,csv,os,sys,subprocess,shutil,statistics as st,math,struct,importlib.util,traceback
from run_campaign import P,save,sha,archive,gate,write_tasks
import formal_support as m
parameter=[];graph=[];built=[]
def status(s):
 save(P/'outputs/extensions_progress.json',{'utc':m.now(),'current':s,'parameter_rounds_done':len(parameter),'parameter_rounds_total':33,'graph_rounds_done':len(graph),'graph_rounds_total':9,'new_extension_indices_done':len(built),'new_extension_indices_total':18})
 print(s,flush=True)
def read(p):return list(csv.DictReader(Path(p).open()))
def validate(raw,query,alpha,ef,variant):
 rr=read(raw);qq=read(query);assert len(rr)==len(qq)==1000
 for r,q in zip(rr,qq):
  assert all(r[k]==q[k] for k in ['query_tag','query_id','row_index','source_vertex']);assert r['region']=='NYC' and r['k']=='10' and int(r['rest_ef'])==ef and r['variant']==variant
  if alpha is not None:assert int(r.get('alpha',2 if alpha==2 else -1))==alpha and r['tau_m']=='5000'
  else:assert r['tau_m']=='6000'
  ids=r['result_ids'].split(';') if r['result_ids'] else [];scores=r['exact_scores'].split(';') if r['exact_scores'] else []
  assert len(ids)==len(set(ids))==len(scores)==int(r['returned_objects'])<=int(r['recall_denominator'])
  assert int(r['recall_denominator'])==min(10,int(r['GT_result_count']))
  assert int(r['extra_assertion_h2h'])==len(ids) and all(math.isfinite(float(x)) for x in scores)
  assert math.isfinite(float(r['core_query_ms'])) and float(r['core_query_ms'])>0 and 0<=float(r['score_recall'])<=1
 return {'QPS':1000/st.mean(float(r['core_query_ms']) for r in rr),'Recall':st.mean(float(r['score_recall']) for r in rr),'empty_results':sum(int(r['returned_objects'])==0 for r in rr),'under_k_returns':sum(int(r['returned_objects'])<10 for r in rr),'raw_file':str(raw),'raw_sha256':sha(raw),'queries':1000}
def run(label,args,alpha):
 status(label);env={**m.ENV,'STUDENT_FREQ_ALPHA':str(alpha)}
 save(P/f'logs/{label}.start.json',{'utc':m.now(),'argv':list(map(str,args)),'environment':env,'loadavg':os.getloadavg()})
 with (P/f'logs/{label}.log').open('x') as f:r=subprocess.run(list(map(str,args)),env=env,cwd=P,stdout=f,stderr=subprocess.STDOUT)
 save(P/f'logs/{label}.exit.json',{'utc':m.now(),'exit_code':r.returncode});assert r.returncode==0,(label,r.returncode)
def load_module(path,name):
 spec=importlib.util.spec_from_file_location(name,path);mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);return mod
def parameters():
 q=P/'queries/NYC_medium.csv';refs=json.loads((P/'configs/parameter_reused.json').read_text())
 for r in refs:
  assert sha(r['raw_file'])==r['raw_sha256'];parameter.append({**r,**validate(r['local_copy'],q,r['alpha'],1600,'full'),'source_file':r['raw_file']})
 for r in read(P/'outputs/performance_rounds.csv'):
  if r['config_id']=='NYC_t5000_full_ef1600_medium':parameter.append({**validate(r['raw_file'],q,64,1600,'full'),'alpha':64,'repetition':int(r['repetition']),'reused':True,'source_file':r['raw_file'],'binding_file':r['index_binding'],'source_version':'general threshold final alpha64; cross-experiment reuse'})
 assert len(parameter)==17
 save(P/'outputs/alpha_parameter_rounds.json',parameter)
 c=next(c for c in m.CFG if c['config_id']=='NYC_t5000_full_ef1600_medium')
 for alpha in [4,8,16,96,128,256,512,1024]:
  reference=next(r for r in refs if r['alpha']==alpha);ref=json.loads(Path(reference['binding_file']).read_text());ref_inst=Path(ref['instance'])
  for rep in [2,3]:
   gate();out=P/f'parameter_runs/a{alpha}_rep{rep}';out.mkdir(parents=True);inst=P/f'instances/parameter_NYC/a{alpha}_rep{rep}'
   run(f'parameter_build_a{alpha}_rep{rep}',[P/'bin/freq_perf','build','NYC',inst,out/'cost','-',alpha],alpha)
   binding={'alpha':alpha,'region':'NYC','repetition':rep,'instance':str(inst),'files':{x.name:{'bytes':x.stat().st_size,'sha256':sha(x)} for x in inst.iterdir() if x.is_file()},'random_policy':'independent build; uncontrolled sources retained; reproduce by index hash'}
   for name in ['frequency_state.bin','mars_aux.bin']:assert binding['files'][name]['sha256']==ref['files'][name]['sha256'],('mapping differs from accepted same-alpha instance',alpha,name)
   ident=json.loads((inst/'identity.json').read_text());assert ident['alpha']==alpha
   with (inst/'rest_graph.bin').open('rb') as f:assert struct.unpack('<QQQQ',f.read(32))==(ident['rest_associations'],32,768,768)
   save(out/'index_binding.json',binding);save(out/'MAPPING_PASS.json',{'frequency_and_all_object_associations_equal_accepted_same_alpha':True,'reference':reference['binding_file'],'actual_alpha':alpha});built.append({'kind':'parameter','alpha':alpha,'rep':rep,'index':str(inst)})
   task=out/'task.tsv';write_tasks(task,[c],alpha);run(f'parameter_perf_a{alpha}_rep{rep}',[P/'bin/freq_perf','perf','NYC',inst,out/'perf',task,alpha],alpha)
   raw=out/'perf/raw'/(c['config_id']+'.csv');row={**validate(raw,q,alpha,1600,'full'),'alpha':alpha,'repetition':rep,'reused':False,'source_file':str(raw),'binding_file':str(out/'index_binding.json'),'source_version':'general threshold implementation identical scoring/search to final alpha64'};parameter.append(row);save(P/'outputs/alpha_parameter_rounds.json',parameter);status(f'PARAMETER_VALIDATED_a{alpha}_rep{rep}');archive(inst)
 assert len(parameter)==33
 for a in [2,4,8,16,32,64,96,128,256,512,1024]:assert {r['repetition'] for r in parameter if r['alpha']==a}=={1,2,3}
 save(P/'ALPHA_PARAMETER_COMPLETE.json',{'rounds':33,'new_rounds':16,'shared_with_main_alpha64':3,'previous_rounds':14,'new_indices':16,'alpha2_historical_specialized_source_caveat':True})
def graphs():
 refs=json.loads((P/'configs/graph_reused.json').read_text());q=P/'queries/NYC_medium.csv'
 for r in refs:
  assert sha(r['raw_file'])==r['raw_sha256'];graph.append({**r,**validate(r['raw_file'],q,None,r['ef'],'graph_shared_category')})
 save(P/'outputs/graph_only_rounds.json',graph)
 for rep in [2,3]:
  gate();D=P/f'graph_only/rep{rep}';status(f'GRAPH_BUILD_rep{rep}');b=load_module(D/'run_shared.py',f'graph_build_{rep}');b.resource_gate();b.run('stats');scale=json.loads((D/'results/scale.json').read_text());assert scale['graph_nodes']==1141457 and scale['all_associations']==4328500
  b.run('build');b.run('reload');aa=read(D/'results/in_memory.csv');bb=read(D/'results/reloaded.csv');assert len(aa)==len(bb)==100
  for x,y in zip(aa,bb):assert all(x[k]==y[k] for k in x if k!='query_ms')
  assert sha(D/'results/in_memory_graph_hits.tsv')==sha(D/'results/reloaded_graph_hits.tsv');b.road_audit(bb)
  binding={'repetition':rep,'index_root':str(D/'index'),'files':{x.name:{'bytes':x.stat().st_size,'sha256':sha(x)} for x in (D/'index').iterdir() if x.is_file()},'category_ID_nodes':1058,'name_nodes':1140399,'associations':4328500,'CPU':96,'NUMA':4,'alpha_independent':True};save(D/'INDEX_ACCEPTED.json',binding);built.append({'kind':'graph','rep':rep,'index':str(D/'index')})
  g=load_module(D/'run_sweep.py',f'graph_perf_{rep}')
  for ef in [1600,3200,6400]:
   status(f'GRAPH_PERFORMANCE_rep{rep}_ef{ef}');rs=g.run_one(ef);raw=D/f'results/ef{ef}/NYC_graph_only_shared_medium1000.csv';result=validate(raw,q,None,ef,'graph_shared_category')
   if ef==1600:
    for x,y in zip(rs[:100],bb):assert x['result_ids']==y['result_ids'] and float(x['score_recall'])==float(y['Recall'])
   graph.append({**result,'ef':ef,'repetition':rep,'reused':False,'index_root':str(D/'index'),'binding_file':str(D/'INDEX_ACCEPTED.json'),'cpu':96,'numa':4});save(P/'outputs/graph_only_rounds.json',graph)
  archive(D/'index')
 assert len(graph)==9
 for ef in [1600,3200,6400]:assert {x['repetition'] for x in graph if x['ef']==ef}=={1,2,3}
 save(P/'GRAPH_COMPLETE.json',{'rounds':9,'new_rounds':6,'reused_rounds':3,'independent_instances':3,'ef_values':[1600,3200,6400],'old_ef800_record_preserved_not_expanded':True})
if __name__=='__main__':
 try:
  parameters();graphs();save(P/'EXTENSIONS_COMPLETE.json',{'alpha_parameter_rounds':33,'graph_rounds':9,'new_indices':18});status('EXTENSIONS_COMPLETE')
 except Exception as e:save(P/'EXTENSIONS_STOPPED_ERROR.json',{'utc':m.now(),'error':str(e),'traceback':traceback.format_exc()});raise
