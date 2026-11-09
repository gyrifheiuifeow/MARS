from pathlib import Path
import json,csv,shutil,hashlib,os,subprocess,struct,datetime
S=Path('/workspace/MARSExp2/submission_experiments_20260726/results/student_runs')
P=S/'student_mars_alpha64_final_20261017_v1'; A=S/'student_mars_freq_general_a64_pilot_20261015_v1'; F=S/'student_mars_freq_a32_formal_20261015_v1'; B=S/'student_mars_freq_general_a32_pilot_20261015_v1'
def save(p,x):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,indent=2)+'\n')
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(8*1024*1024),b''):h.update(b)
 return h.hexdigest()
def rows(p):return list(csv.DictReader(Path(p).open()))
def tasks(p,rs):
 with p.open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=list(rs[0]),delimiter='\t',lineterminator='\n');w.writeheader();w.writerows(rs)
assert not (P/'src_tree').exists()
for d in ['bin','configs','queries','provenance','logs','outputs','acceptance','instances','runs','references']:(P/d).mkdir(exist_ok=True)
for d in ['src_tree','drivers']:shutil.copytree(A/d,P/d)
frozen=json.loads((A/'PILOT_FREEZE.json').read_text());checked={}
for rel,h in frozen['files_sha256'].items():
 if rel.startswith(('src_tree/','drivers/')):assert sha(P/rel)==sha(A/rel)==h;checked[rel]=h
save(P/'provenance/unchanged_source_core.json',checked)
cfg=json.loads((F/'configs/unique_performance_configs.json').read_text());diag=json.loads((F/'configs/diagnostic_config_ids.json').read_text())
for c in cfg:
 q=Path(c['query_manifest']);dest=P/'queries'/q.name
 if not dest.exists():shutil.copy2(q,dest)
 assert sha(dest)==sha(q) and len(rows(dest))==1000
 c.update(alpha=64,query_manifest=str(dest))
save(P/'configs/unique_performance_configs.json',cfg);save(P/'configs/diagnostic_config_ids.json',diag)
for n in ['cross_experiment_reuse.csv','exact_road_units.tsv']:shutil.copy2(F/'configs'/n,P/'configs'/n)
for n in ['expected_dataset_bindings.json','effective_GT_bindings.json','expected_query_GT_hashes.json']:shutil.copy2(F/'provenance'/n,P/'provenance'/n)
env=json.loads((A/'configs/environment.json').read_text());env.update(EXACT_ROAD_UNITS_MANIFEST=str(P/'configs/exact_road_units.tsv'),STUDENT_FREQ_ALPHA='64');save(P/'configs/environment.json',env)
for reg in ['BAY','COL']:
 shutil.copy2(F/f'queries/accept_{reg}.csv',P/f'queries/accept_{reg}.csv')
 tasks(P/f'configs/accept_{reg}.tsv',[{'config_id':f'accept_{reg}_{v}','tau':6000,'k':10,'variant':v,'rest_ef':1600,'query_manifest':str(P/f'queries/accept_{reg}.csv'),'workload':'acceptance_fixed','alpha':64} for v in ['full','pivot','pointwise']])
for reg in ['NYC','BAY','COL','FLA']:
 selected=[c for c in cfg if c['region']==reg]
 def task(c):return {'config_id':c['config_id'],'tau':c['tau_m'],'k':c['k'],'variant':c['variant'],'rest_ef':c['rest_ef'],'query_manifest':c['query_manifest'],'workload':c['workload'],'alpha':64}
 tasks(P/f'configs/perf_{reg}.tsv',[task(c) for c in selected]);tasks(P/f'configs/diag_{reg}.tsv',[task(c) for c in selected if c['config_id'] in diag])
# Hash the input files before starting measurements.
data=json.loads((F/'provenance/expected_dataset_bindings.json').read_text());file_hash={}
for reg,d in data.items():
 for path,h in d['data_sha256'].items():assert sha(path)==h;file_hash[path]=h
 print('DATA_PASS',reg,flush=True)
for path,h in json.loads((F/'provenance/expected_query_GT_hashes.json').read_text()).items():assert sha(path)==h;file_hash[path]=h
save(P/'provenance/input_hashes.json',file_hash)
for reg in ['NYC','FLA']:
 binding=json.loads((A/f'runs/{reg}/index_binding.json').read_text());inst=Path(binding['instance']);ident=json.loads((inst/'identity.json').read_text());assert ident['alpha']==64 and ident['objects']==data[reg]['objects']
 for name,info in binding['files'].items():assert (inst/name).stat().st_size==info['bytes'] and sha(inst/name)==info['sha256']
 ac=json.loads((A/f'runs/{reg}/ACCEPTANCE_PASS.json').read_text());assert ac['full_associations_and_expected_pivot_count'] and ac['roundtrip'] and ac['full_direct_equal_both_alpha32_and64']
 out=P/f'runs/{reg}_rep1';out.mkdir();shutil.copytree(A/f'runs/{reg}/cost',out/'cost')
 save(out/'index_binding.json',{**binding,'reused':True,'repetition':1,'source_binding':str(A/f'runs/{reg}/index_binding.json')});save(out/'ACCEPTANCE_PASS.json',{'reused_qualified_acceptance':True,'source':str(A/f'runs/{reg}/ACCEPTANCE_PASS.json')})
 print('INDEX_REUSE_PASS',reg,flush=True)
reused=json.loads((A/'comparison_full1000_20261015_v1/outputs/new_performance_results.json').read_text())
extra=A/'medium6_full1000_20261016_v1'
raws=[x for x in extra.rglob('*.csv') if x.name=='NYC_t6000_full_ef1600_medium.csv' and 'delivery' not in str(x)]
assert raws
raw=raws[0];assert all(sha(x)==sha(raw) for x in raws)
reused.append({'config_id':'NYC_t6000_full_ef1600_medium','raw_file':str(raw),'raw_sha256':sha(raw)})
for r in reused:
 assert sha(r['raw_file'])==r['raw_sha256'];c=next(c for c in cfg if c['config_id']==r['config_id']);rr=rows(r['raw_file']);qq=rows(c['query_manifest']);assert len(rr)==len(qq)==1000
 for x,q in zip(rr,qq):
  assert all(x[k]==q[k] for k in ['query_tag','query_id','row_index','source_vertex'])
  assert x['alpha']=='64' and all(x[k]==str(c[k]) for k in ['region','tau_m','k','variant','rest_ef','workload'])
 r.update(region=c['region'],repetition=1)
save(P/'configs/reused_performance.json',reused)
baseline=S/'student_exp5_baselines_query_groups_20261015_v2/execution_v4'
assert json.loads((baseline/'COMPLETED.json').read_text())['formal_rounds']==36
save(P/'references/query_group_baselines.json',{'source':str(baseline),'rounds':36,'audit':json.loads((baseline/'FINAL_DELIVERY_AUDIT_20261015.json').read_text()),'preserve_external_activity_caveat':True})
nas=Path('/mnt/nas_mount/anonymous_MARS_student_archives')/P.name;nas.mkdir(parents=True,exist_ok=False)
probe=nas/'ARCHIVE_POLICY.json';save(probe,{'owner':'anonymous','campaign':str(P),'only_new_campaign_completed_indices':True,'preserve_originals_and_old_frozen_indices':True,'verify_copy_before_replacing_new_local_directory_with_symlink':True,'archive_between_measurements_only':True})
assert shutil.disk_usage(nas).free>400_000_000_000
save(P/'configs/storage.json',{'archive_root':str(nas),'local_student_free_bytes':shutil.disk_usage(P).free,'archive_free_bytes':shutil.disk_usage(nas).free,'local_reserve_bytes':35_000_000_000,'build_and_save_on_original_data_device':True,'archive_only_after_all_instance_measurements':True})
plan={'default_alpha':64,'default_ef':1600,'main_performance':144,'main_reused_performance':5,'main_diagnostic':48,'default_indices':12,'default_reused_indices':2,'default_new_indices':10,'parameter_alpha_points':[2,4,8,16,32,64,96,128,256,512,1024],'parameter_ef_fixed':1600,'parameter_alpha_three_instances':True,'alpha64_parameter_point_shared_with_main':True,'parameter_other_reused_rounds_expected':14,'parameter_new_indices_expected':16,'graph_only_efs':[1600,3200,6400],'graph_only_indices_total':3,'graph_only_new_indices_expected':2,'graph_only_reused_rounds':3,'graph_only_new_rounds_expected':6,'query_groups_baseline_reused_rounds':36,'no_paper_changes':True,'old_category_results_separate':True,'no_additional_parameter_selection':True,'three_independent_indices_not_repeated_queries_on_one_index':True,'serial_all_stages':True,'graph_cpu_policy':'preserve CPU96 strict NUMA4 for Graph-only; MARS inherited unpinned single-thread policy; disclose rather than claim identical affinity','baseline_caveat':'10 historical query-group run windows observed external processes; retain explicitly disclosed source caveat'}
save(P/'configs/APPROVED_PLAN.json',plan)
commands=json.loads((B/'provenance/builds.json').read_text());built=[]
for mode in ['accept','perf','diag']:
 cmd=[v.replace(str(B),str(P)) for v in next(x for x in commands if x['mode']==mode)['argv']];print('COMPILE',mode,flush=True)
 with (P/f'logs/compile_{mode}.log').open('x') as log:r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT)
 assert r.returncode==0;built.append({'mode':mode,'argv':cmd,'sha256':sha(P/f'bin/freq_{mode}')})
 if mode=='accept':
  with (P/'logs/toy.log').open('x') as log:r=subprocess.run([str(P/'bin/freq_accept'),'unit',str(P/'acceptance/toy')],env=env,stdout=log,stderr=subprocess.STDOUT)
  assert r.returncode==0
save(P/'provenance/builds.json',built);save(P/'provenance/PREFLIGHT_PASS.json',{'alpha':64,'reused_indices':2,'reused_performance':5,'source_core_unchanged':True,'data_and_query_GT_hashes_checked':True,'synthetic_alpha64_coverage_inherited_and_rerun':True});print('PREFLIGHT_PASS',flush=True)
