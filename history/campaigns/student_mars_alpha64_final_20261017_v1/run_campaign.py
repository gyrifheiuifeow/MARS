from pathlib import Path
import json,csv,os,sys,subprocess,shutil,hashlib,datetime,traceback,fcntl,struct,statistics,math
import formal_support as m
P=Path(__file__).resolve().parent
def save(p,v):m.save(p,v)
def sha(p):return m.sha(p)
def write_tasks(p,cs,alpha=64):
 rs=[{'config_id':c['config_id'],'tau':c['tau_m'],'k':c['k'],'variant':c['variant'],'rest_ef':c['rest_ef'],'query_manifest':c['query_manifest'],'workload':c['workload'],'alpha':alpha} for c in cs]
 with p.open('x',newline='') as f:w=csv.DictWriter(f,fieldnames=list(rs[0]),delimiter='\t',lineterminator='\n');w.writeheader();w.writerows(rs)
def archive(directory):
 directory=Path(directory)
 if not directory.exists() or directory.is_symlink():return
 assert directory.resolve().is_relative_to(P.resolve()) and directory!=P
 dest=Path(json.loads((P/'configs/storage.json').read_text())['archive_root'])/directory.relative_to(P)
 assert not dest.exists();dest.parent.mkdir(parents=True,exist_ok=True)
 save(P/'outputs/archive_progress.json',{'utc':m.now(),'current':'ARCHIVE_NEW_COMPLETED_'+str(directory.relative_to(P))})
 before={str(x.relative_to(directory)):{'bytes':x.stat().st_size,'sha256':sha(x)} for x in directory.rglob('*') if x.is_file()}
 shutil.copytree(directory,dest)
 for rel,info in before.items():assert (dest/rel).stat().st_size==info['bytes'] and sha(dest/rel)==info['sha256']
 save(P/'provenance/archive_records'/ (str(directory.relative_to(P)).replace('/','_')+'.json'),{'original_path':str(directory),'archive_path':str(dest),'files':before,'copy_verified_before_local_release':True,'only_new_campaign_artifacts':True})
 # Verify the archived copy before replacing the local directory with a symlink.
 shutil.rmtree(directory);directory.symlink_to(dest,target_is_directory=True)
def gate():
 assert shutil.disk_usage(P).free>35_000_000_000,'Insufficient student scratch capacity; preserve all records'
 tasks=subprocess.check_output(['ps','-u',str(os.getuid()),'-o','pid,args'],text=True)
 # Avoid overlapping benchmark workers.
 active=[x for x in tasks.splitlines() if any(k in x for k in ['bin/freq_perf ','bin/freq_diag ','runtime/measure_once ','runtime/shared_graph build','bin/baseline_entry '])]
 assert not active,('Another benchmark is running',active)
def main_stage():
 for reg in ['NYC','FLA']:m.qualify_build(reg,1,True)
 for r in json.loads((P/'configs/reused_performance.json').read_text()):
  cid=r['config_id'];reg=r['region'];out=P/f'runs/{reg}_rep1/perf/raw';out.mkdir(parents=True,exist_ok=True);dest=out/(cid+'.csv');shutil.copy2(r['raw_file'],dest);assert sha(dest)==r['raw_sha256']
  row=m.validate(reg,1,'perf',cid,dest);row.update(reused=True,source_file=r['raw_file'],source_sha256=r['raw_sha256']);m.perf.append(row)
 m.refresh('REUSED_FIVE_FORMAL_ROUNDS')
 # Process indexes serially to limit disk usage.
 for reg in ['BAY','COL','NYC','FLA']:
  for rep in [1,2,3]:
   gate()
   if (reg,rep) not in m.bindings:m.construct(reg,rep)
   out=P/f'runs/{reg}_rep{rep}';inst=m.bindings[reg,rep]['instance']
   for mode in ['perf','diag']:
    selected=[c for c in m.CFG if c['region']==reg and (mode=='perf' or c['config_id'] in m.DIAG)]
    prior={r['config_id'] for r in (m.perf if mode=='perf' else m.diag) if r['region']==reg and r['repetition']==rep};missing=[c for c in selected if c['config_id'] not in prior]
    folder=out/mode
    if missing:
     # Write new measurements beside the reused results.
     fresh=out/(mode+'_new');task=P/f'configs/runtime_{mode}_{reg}_{rep}.tsv';write_tasks(task,missing)
     m.run(f'{mode}_{reg}_{rep}',[P/f'bin/freq_{mode}',mode,reg,inst,fresh,task,64])
     (folder/'raw').mkdir(parents=True,exist_ok=True)
     for c in missing:
      cid=c['config_id'];raw=fresh/'raw'/(cid+'.csv');row=m.validate(reg,rep,mode,cid,raw);row.update(reused=False,source_file=str(raw),source_sha256=sha(raw));(m.perf if mode=='perf' else m.diag).append(row)
      dest=folder/'raw'/raw.name;assert not dest.exists();shutil.copy2(raw,dest)
    for c in selected:
     raw=folder/'raw'/(c['config_id']+'.csv')
     if mode=='diag':m.same(raw,out/'perf/raw'/raw.name,1000)
     if c['variant']=='pointwise':m.same(raw,folder/'raw'/(c['config_id'].replace('_pointwise_','_full_')+'.csv'),1000)
    save(folder/'VALIDATED.json',{'records':[r for r in (m.perf if mode=='perf' else m.diag) if r['region']==reg and r['repetition']==rep]});m.refresh('VALIDATED_'+mode+'_'+reg+'_'+str(rep))
   if not m.bindings[reg,rep].get('reused'):archive(Path(inst))
   if (out/'acceptance').exists():archive(out/'acceptance')
 assert len(m.perf)==144 and len(m.diag)==48 and len(m.builds)==12
 assert len({(x['config_id'],x['repetition']) for x in m.perf})==144
 for reg in ['NYC','BAY','COL','FLA']:assert len({m.bindings[reg,i]['files']['rest_graph.bin']['sha256'] for i in [1,2,3]})==3
 save(P/'MAIN_COMPLETE.json',{'performance':144,'diagnostics':48,'indices':12,'reused_performance':5,'new_performance':139,'reused_indices':2,'new_indices':10})
def main():
 lock=(P/'campaign.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB);assert not (P/'STARTED.json').exists()
 freeze=json.loads((P/'FORMAL_FREEZE.json').read_text())
 for rel,h in freeze['files_sha256'].items():assert sha(P/rel)==h,rel
 save(P/'STARTED.json',{'utc':m.now(),'pid':os.getpid(),'default_alpha':64,'all_stages_authorized':True});main_stage()
 m.refresh('PARAMETER_AND_GRAPH_COMPLETION')
 subprocess.run([sys.executable,P/'run_extensions.py'],cwd=P,check=True)
 m.refresh('EXPORTING_UNIFIED_RESULTS');subprocess.run([sys.executable,P/'export_final.py'],cwd=P,check=True)
 save(P/'COMPLETED.json',{'utc':m.now(),'default_alpha':64,'main_performance':144,'diagnostic':48,'default_builds':12,'extensions_verified':True,'export_completed':True});m.refresh('COMPLETE')
if __name__=='__main__':
 try:main()
 except Exception as e:save(P/'STOPPED_ERROR.json',{'utc':m.now(),'error':str(e),'traceback':traceback.format_exc(),'preserve_all_records':True,'do_not_restart_completed_measurements':True});print(traceback.format_exc(),flush=True);sys.exit(1)
