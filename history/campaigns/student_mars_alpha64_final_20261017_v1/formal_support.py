from pathlib import Path
import json,csv,subprocess,os,hashlib,statistics as st,datetime,traceback,sys,fcntl,math,struct
P=Path(__file__).resolve().parent;ENV=json.loads((P/'configs/environment.json').read_text());CFG=json.loads((P/'configs/unique_performance_configs.json').read_text());BY={c['config_id']:c for c in CFG};DIAG=json.loads((P/'configs/diagnostic_config_ids.json').read_text())
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def save(p,x):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,indent=2)+'\n')
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(8*1024*1024),b''):h.update(b)
 return h.hexdigest()
def rows(p,sep=','):return list(csv.DictReader(Path(p).open(newline=''),delimiter=sep))
def write(p,rr):
 if not rr:return
 with p.open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=list(dict.fromkeys(k for r in rr for k in r)),lineterminator='\n');w.writeheader();w.writerows(rr)
perf=[];diag=[];builds=[];bindings={};history=[]
def refresh(current):
 save(P/'outputs/progress.json',{'utc':now(),'current':current,'alpha':64,'performance_done':len(perf),'performance_planned':144,'diagnostic_done':len(diag),'diagnostic_planned':48,'qualified_instances':len(builds),'instances_planned':12,'reused_instances':sum(x['reused'] for x in builds),'new_builds_done':sum(not x['reused'] for x in builds),'new_builds_planned':10,'regions_with_first_acceptance':[r for r in ['NYC','BAY','COL','FLA'] if (P/f'runs/{r}_rep1/ACCEPTANCE_PASS.json').exists()]})
 for label,rr in [('performance',perf),('diagnostic',diag),('build',builds)]:write(P/f'outputs/{label}_rounds.csv',rr)
 for label,rr,metrics in [('performance',perf,['QPS','Recall','ID_recall','mean_query_ms']),('diagnostic',diag,['Recall','mean_query_ms','compile_ms','pivot_ms','rest_ms','other_normal_ms','pivot_dot_ms','pivot_sort_ms','pivot_posting_ms','h2h_compile','h2h_pivot','h2h_rest','h2h_core_total','h2h_other','extra_assertion_h2h','extra_assertion_ms','posting_direct','posting_interval','posting_fringe','posting_scan_total'])]:
  summary=[]
  for cid in sorted({r['config_id'] for r in rr}):
   gg=[r for r in rr if r['config_id']==cid];assert len({r['repetition'] for r in gg})==len(gg)<=3;c=BY[cid];out={k:c[k] for k in ['config_id','region','tau_m','k','variant','rest_ef','workload']};out.update(alpha=64,instances=len(gg),experiments=';'.join(c['experiments']))
   for m in metrics:out[m+'_mean']=st.mean(r[m] for r in gg);out[m+'_sample_sd']=st.stdev(r[m] for r in gg) if len(gg)>1 else None
   summary.append(out)
  write(P/f'outputs/{label}_summary.csv',summary)
def event(s,**kw):
 print(s,json.dumps(kw),flush=True)
 with (P/'events.jsonl').open('a') as f:f.write(json.dumps({'utc':now(),'event':s,**kw})+'\n')
def run(label,args):
 log=P/f'logs/{label}.log';assert not log.exists(),('REFUSE_REPEAT',label);refresh(label);event('START',label=label)
 save(P/f'logs/{label}.start.json',{'utc':now(),'argv':list(map(str,args)),'loadavg':os.getloadavg(),'environment':ENV})
 with log.open('x') as f:r=subprocess.run(list(map(str,args)),env=ENV,cwd=P,stdout=f,stderr=subprocess.STDOUT)
 save(P/f'logs/{label}.exit.json',{'utc':now(),'exit_code':r.returncode})
 if r.returncode:raise RuntimeError(f'{label} failed ({r.returncode}); preserve data and inspect {log}')
 history.append(label);event('PROCESS_PASS',label=label)
def same(a,b,count):
 aa=rows(a);bb=rows(b);assert len(aa)==len(bb)==count
 for x,y in zip(aa,bb):
  for k in ['query_tag','query_id','row_index','source_vertex','result_ids','exact_scores','score_recall','id_recall','GT_result_count','recall_denominator']:assert x[k]==y[k],(a,b,x['query_id'],k)
def qualify_build(reg,rep,reused):
 out=P/f'runs/{reg}_rep{rep}';binding=json.loads((out/'index_binding.json').read_text());bindings[reg,rep]=binding
 stages={r['phase']:float(r['exclusive_ms']) for r in rows(out/'cost/build_stages.csv')};space=rows(out/'cost/space.csv');mem=json.loads((out/'cost/build_memory.json').read_text())
 assert not mem['includes_validation_or_evaluation_queries'] and mem['includes_shared_input_load']
 for r in space:assert int(r['serialized_bytes'])-int(r['embedded_shared_payload_bytes'])==int(r['additional_bytes'])
 b={'region':reg,'repetition':rep,'alpha':64,'reused':reused,'shared_load_ms':stages['shared_load'],'method_preprocess_ms':stages['method_preprocess'],'structure_build_ms':stages['structure_build'],'additional_build_ms':stages['method_preprocess']+stages['structure_build'],'save_ms':stages['save'],'serialized_bytes':sum(int(r['serialized_bytes']) for r in space),'explicit_payload_copy_bytes':sum(int(r['embedded_shared_payload_bytes']) for r in space),'structure_bytes':sum(int(r['additional_bytes']) for r in space),'peak_RSS_KiB':mem['process_peak_rss_kib_before_queries'],'cost_source':str(out/'cost'),'index_binding':str(out/'index_binding.json')}
 assert (reg,rep) not in {(x['region'],x['repetition']) for x in builds};builds.append(b);save(out/'QUALIFIED_BUILD.json',b);refresh('QUALIFIED_'+reg+'_'+str(rep))
def construct(reg,rep):
 out=P/f'runs/{reg}_rep{rep}';out.mkdir();inst=P/f'instances/{reg}/rep{rep}'
 run(f'build_{reg}_{rep}',[P/'bin/freq_perf','build',reg,inst,out/'cost','-',64])
 binding={'region':reg,'repetition':rep,'alpha':64,'reused':False,'instance':str(inst),'files':{f.name:{'sha256':sha(f),'bytes':f.stat().st_size} for f in inst.iterdir() if f.is_file()},'random_strategy':'inherited incompletely controlled construction randomness; actual graph files and hashes define reproduction instance'}
 save(out/'index_binding.json',binding)
 if rep==1:
  run(f'partition_{reg}_{rep}',[sys.executable,P/'validate_partition.py',reg,rep])
  task=P/f'configs/accept_{reg}.tsv';count=len(rows(P/f'queries/accept_{reg}.csv'))
  run(f'accept_{reg}_{rep}',[P/'bin/freq_accept','accept',reg,inst,out/'acceptance',task,64])
  for src,dst in [('frequency_state.bin','roundtrip_frequency_state.bin'),('mars_aux.bin','roundtrip_mars_aux.bin'),('rest_graph.bin','roundtrip_rest_graph.bin')]:assert sha(inst/src)==sha(out/'acceptance'/dst)
  run(f'accept_no_instrumentation_{reg}_{rep}',[P/'bin/freq_perf','perf',reg,inst,out/'uninstrumented_acceptance',task,64])
  for variant in ['full','pivot','pointwise']:
   raw=out/f'acceptance/raw/accept_{reg}_{variant}.csv';same(raw,out/f'uninstrumented_acceptance/raw/accept_{reg}_{variant}.csv',count)
   for r in rows(raw):assert all(r[k]=='1' for k in ['reference_pivot_equal','processed_merge_equal','road_variant_equal','roundtrip_results_equal'])
   qid='63' if reg=='BAY' else '845';expect=9 if reg=='BAY' else 5;record=next(r for r in rows(raw) if r['query_id']==qid);assert int(record['recall_denominator'])==expect
  same(out/f'acceptance/raw/accept_{reg}_full.csv',out/f'acceptance/raw/accept_{reg}_pointwise.csv',count)
  save(out/'ACCEPTANCE_PASS.json',{'region':reg,'alpha':64,'all_frequency_associations_and_node_mapping':True,'queries':count,'independent_pivot_integer_road_reference':True,'processed_score_max_aggregation':True,'full_direct_equal':True,'uninstrumented_equal':True,'roundtrip':True,'under_k_query_preserved':True,'not_formal_performance':True})
 else:
  first=bindings[reg,1];assert binding['files']['frequency_state.bin']['sha256']==first['files']['frequency_state.bin']['sha256'];assert binding['files']['mars_aux.bin']['sha256']==first['files']['mars_aux.bin']['sha256'],'deterministic hierarchy/mapping changed'
  with (inst/'rest_graph.bin').open('rb') as f:shape=struct.unpack('<QQQQ',f.read(32))
  ident=json.loads((inst/'identity.json').read_text());assert shape==(ident['rest_associations'],32,768,768) and ident['alpha']==64
  save(out/'MAPPING_BINDING_PASS.json',{'same_frequency_and_aux_as_accepted_first_instance':True,'graph_header_matches_mapping':True})
 qualify_build(reg,rep,False)
def validate(reg,rep,mode,cid,path):
 c=BY[cid];rr=rows(path);queries=rows(c['query_manifest']);assert len(rr)==len(queries)==1000 and len({(x['query_tag'],x['query_id']) for x in rr})==1000
 for x,q in zip(rr,queries):
  assert all(x[k]==q[k] for k in ['query_tag','query_id','row_index','source_vertex']);assert x['alpha']=='64'
  assert all(x[k]==str(c[k]) for k in ['config_id','region','tau_m','k','variant','rest_ef','workload'])
  assert int(x['recall_denominator'])==min(10,int(x['GT_result_count']))
  ids=x['result_ids'].split(';') if x['result_ids'] else [];sc=x['exact_scores'].split(';') if x['exact_scores'] else [];assert len(ids)==len(set(ids))==len(sc)==int(x['returned_objects'])<=int(x['recall_denominator'])
  assert all(math.isfinite(float(v)) for v in sc);assert 0<=float(x['score_recall'])<=1;assert 0<=float(x['id_recall'])<=1;assert math.isfinite(float(x['core_query_ms'])) and float(x['core_query_ms'])>0
 avg=lambda k:st.mean(float(x[k]) for x in rr)
 r={k:c[k] for k in ['config_id','region','tau_m','k','variant','rest_ef','workload']};r.update(alpha=64,repetition=rep,mode=mode,queries=1000,experiments=';'.join(c['experiments']),QPS=1000/avg('core_query_ms') if mode=='perf' else None,Recall=avg('score_recall'),ID_recall=avg('id_recall'),mean_query_ms=avg('core_query_ms'),mean_returned_objects=avg('returned_objects'),empty_results=sum(int(x['returned_objects'])==0 for x in rr),under_k_GT_queries=sum(int(x['GT_result_count'])<10 for x in rr),raw_file=str(path),raw_sha256=sha(path),index_binding=str(P/f'runs/{reg}_rep{rep}/index_binding.json'))
 if mode=='diag':
  for k in ['compile_ms','pivot_ms','rest_ms','pivot_dot_ms','pivot_sort_ms','pivot_posting_ms','pivot_subphase_overhead_ms','h2h_compile','h2h_pivot','h2h_rest','h2h_core_total','extra_assertion_h2h','extra_assertion_ms','posting_direct','posting_interval','posting_fringe','posting_scan_total']:r[k]=avg(k)
  r['other_normal_ms']=sum(avg(k) for k in ['init_ms','result_materialization_ms','cleanup_return_ms']);r['h2h_other']=sum(avg(k) for k in ['h2h_init','h2h_result','h2h_cleanup'])
  for x in rr:
   assert abs(float(x['phase_error_ms']))<1e-8
   assert abs(sum(float(x[k]) for k in ['pivot_dot_ms','pivot_sort_ms','pivot_posting_ms','pivot_subphase_overhead_ms'])-float(x['pivot_ms']))<1e-8
   assert int(x['h2h_core_total'])==sum(int(x[k]) for k in ['h2h_init','h2h_compile','h2h_pivot','h2h_rest','h2h_result','h2h_cleanup'])
   assert int(x['h2h_compile'])==int(x['bound_h2h_requests']);assert int(x['posting_scan_total'])==sum(int(x[k]) for k in ['posting_direct','posting_interval','posting_fringe'])
 return r
def measure(reg,rep):
 assert (P/f'runs/{reg}_rep1/ACCEPTANCE_PASS.json').exists();out=P/f'runs/{reg}_rep{rep}';inst=bindings[reg,rep]['instance'];ids=[c['config_id'] for c in CFG if c['region']==reg]
 for mode in ['perf','diag']:
  folder=out/mode;task=P/f'configs/{mode}_{reg}.tsv';run(f'{mode}_{reg}_{rep}',[P/f'bin/freq_{mode}',mode,reg,inst,folder,task,64])
  selected=ids if mode=='perf' else [c for c in ids if c in DIAG];stats=[]
  for cid in selected:
   raw=folder/'raw'/(cid+'.csv');stats.append(validate(reg,rep,mode,cid,raw))
   if mode=='diag':same(raw,out/'perf/raw'/(cid+'.csv'),1000)
  for cid in selected:
   if BY[cid]['variant']=='pointwise':same(folder/'raw'/(cid+'.csv'),folder/'raw'/(cid.replace('_pointwise_','_full_')+'.csv'),1000)
  save(folder/'VALIDATED.json',{'mode':mode,'records':stats});(perf if mode=='perf' else diag).extend(stats);refresh('VALIDATED_'+mode+'_'+reg+'_'+str(rep))
 event('INSTANCE_MEASUREMENTS_COMPLETE',region=reg,repetition=rep)
def main():
 assert not (P/'STARTED.json').exists() and not (P/'STOPPED_ERROR.json').exists();lock=(P/'formal.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 freeze=json.loads((P/'FORMAL_FREEZE.json').read_text())
 for rel,h in freeze['files_sha256'].items():assert sha(P/rel)==h
 save(P/'STARTED.json',{'utc':now(),'pid':os.getpid(),'user_confirmed_alpha':32,'performance_rounds':144,'diagnostic_rounds':48,'construction_instances':12,'reused_indices':2,'new_builds':10,'other_baselines_not_rerun':True})
 for reg in ['NYC','FLA']:qualify_build(reg,1,True);event('REUSED_INSTANCE_QUALIFIED',region=reg,repetition=1)
 for reg in ['BAY','COL']:construct(reg,1)
 assert all((P/f'runs/{reg}_rep1/ACCEPTANCE_PASS.json').exists() for reg in ['NYC','BAY','COL','FLA']);event('ALL_FOUR_REGIONS_ACCEPTED')
 for reg in ['NYC','BAY','COL','FLA']:
  for rep in [1,2,3]:
   if (reg,rep) not in bindings:construct(reg,rep)
   measure(reg,rep)
 assert len(perf)==144 and len(diag)==48 and len(builds)==12
 assert {(r['config_id'],r['repetition']) for r in perf}=={(c,i) for c in BY for i in [1,2,3]}
 assert {(r['config_id'],r['repetition']) for r in diag}=={(c,i) for c in DIAG for i in [1,2,3]}
 for reg in ['NYC','BAY','COL','FLA']:assert len({bindings[reg,i]['files']['rest_graph.bin']['sha256'] for i in [1,2,3]})==3
 for rel,h in freeze['files_sha256'].items():assert sha(P/rel)==h
 save(P/'COMPLETED.json',{'utc':now(),'alpha':64,'validated_performance':144,'validated_diagnostics':48,'qualified_instances':12,'reused_builds':2,'new_builds':10,'three_independent_instance_coverage':True,'pilot_100_query_results_not_used_as_formal':True,'other_baselines_not_rerun':True,'old_results_not_overwritten':True});refresh('COMPLETE');event('COMPLETE')
if __name__=='__unused_old_main__':
 try:main()
 except Exception as e:save(P/'STOPPED_ERROR.json',{'utc':now(),'error':str(e),'traceback':traceback.format_exc(),'all_data_retained':True,'no_auto_retry':True});event('STOPPED',error=str(e));sys.exit(1)
