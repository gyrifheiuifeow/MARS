from pathlib import Path
import json,csv,statistics as st,hashlib,shutil,html,zipfile,collections
P=Path(__file__).resolve().parent;S=P.parent;D=P/'final_delivery_v1'
def rows(p):return list(csv.DictReader(Path(p).open(encoding='utf-8-sig')))
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(8*1024*1024),b''):h.update(b)
 return h.hexdigest()
def save(p,x):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,indent=2,ensure_ascii=False)+'\n')
def write(name,rr):
 assert rr;keys=list(dict.fromkeys(k for r in rr for k in r));p=D/name;p.parent.mkdir(parents=True,exist_ok=True)
 with p.open('x',encoding='utf-8-sig',newline='') as f:w=csv.DictWriter(f,fieldnames=keys,lineterminator='\n');w.writeheader();w.writerows(rr)
def summary(rr,keys,metrics):
 groups=collections.defaultdict(list)
 for r in rr:groups[tuple(str(r[k]) for k in keys)].append(r)
 ans=[]
 for key,rs in sorted(groups.items()):
  assert len(rs)==3,(key,len(rs));assert len({str(r['repetition']) for r in rs})==3
  out=dict(zip(keys,key));out['instances']=3
  for metric in metrics:
   vals=[float(r[metric]) for r in rs];out[metric+'_mean']=st.mean(vals);out[metric+'_sample_sd']=st.stdev(vals)
  ans.append(out)
 return ans
def copy_raw(src,expected,rel):
 src=Path(src);assert sha(src)==expected,src;p=D/'raw'/rel;p.parent.mkdir(parents=True,exist_ok=True)
 if p.exists():assert sha(p)==expected
 else:shutil.copy2(src,p)
 return str(p.relative_to(D))
assert (P/'MAIN_COMPLETE.json').exists() and (P/'EXTENSIONS_COMPLETE.json').exists();assert not D.exists();D.mkdir()
perf=rows(P/'outputs/performance_rounds.csv');diag=rows(P/'outputs/diagnostic_rounds.csv');build=rows(P/'outputs/build_rounds.csv');assert (len(perf),len(diag),len(build))==(144,48,12)
for mode,rs in [('performance',perf),('diagnostic',diag)]:
 for r in rs:r['package_raw']=copy_raw(r['raw_file'],r['raw_sha256'],f"mars64/{r['region']}_rep{r['repetition']}/{mode}/{r['config_id']}.csv")
 write('MARS64_'+mode+'_rounds.csv',rs)
ps=summary(perf,['config_id','region','tau_m','k','variant','rest_ef','workload'],['QPS','Recall','mean_query_ms']);write('MARS64_performance_summary.csv',ps)
old=S/'student_hnsw_empty_restart_candidate_20261008_v1/formal_runs/student_hnsw_formal_20261008_v1/outputs'
main_old=rows(next(old.glob('Exp1-3*完整*逐轮*.csv')));assert len(main_old)==540
main=[r for r in main_old if r['method']!='MARS'];assert len(main)==468
for r in main:r['package_raw']=copy_raw(r['raw_file'],r['raw_sha256'],f"baselines/{r['method']}/{r['region']}_rep{r['repetition']}_tau{r['tau_m']}_limit{r['candidate_limit']}.csv")
lookup={(r['region'],r['repetition'],r['tau_m']):r for r in perf if r['variant']=='full' and r['rest_ef']=='1600' and r['workload']=='medium'}
for oldr in main_old:
 if oldr['method']!='MARS':continue
 r=lookup[oldr['region'],oldr['repetition'],oldr['tau_m']]
 main.append({**oldr,**{k:r[k] for k in ['QPS','Recall','mean_query_ms','raw_file','raw_sha256','package_raw']},'alpha':64,'source_version':P.name,'search_policy':'frequency64_corrected_pivot_channel_only','query_program_sha256':'see current FORMAL_FREEZE and per-run source binding'})
assert len(main)==540;write('Exp1_3_final_rounds.csv',main)
ms=summary(main,['method','region','tau_m','candidate_limit'],['QPS','Recall','mean_query_ms']);write('Exp1_3_final_summary.csv',ms)
rank=[]
for reg in ['NYC','BAY','COL','FLA']:
 rs=[r for r in ms if r['region']==reg and r['tau_m']=='6000'];assert len(rs)==5
 for pos,r in enumerate(sorted(rs,key=lambda x:-x['QPS_mean']),1):rank.append({**r,'throughput_rank':pos,'MARS_QPS_over_method':next(x['QPS_mean'] for x in rs if x['method']=='MARS')/r['QPS_mean']})
write('Exp1_rankings_and_speedups.csv',rank)
group_source=S/'student_exp5_baselines_query_groups_20261015_v2/execution_v4';gr=[]
for r in rows(group_source/'outputs/per_round.csv'):
 gr.append({**r,'workload':r['group'],'repetition':r['rep'],'Recall':r['Recall_audit'],'package_raw':copy_raw(r['raw_file'],r['raw_sha256'],f"query_groups/{r['method']}_rep{r['rep']}_{r['group']}.csv")})
assert len(gr)==36
for r in perf:
 if r['region']=='NYC' and r['tau_m']=='5000' and r['rest_ef']=='1600' and r['variant']=='full':gr.append({**r,'method':'MARS','external_activity_observed':'not claimed absent; serial current campaign'})
assert len(gr)==45;write('Exp5_all_methods_rounds.csv',gr);gs=summary(gr,['method','workload'],['QPS','Recall']);write('Exp5_all_methods_summary.csv',gs)
for number,select in [(4,lambda r:r['region']=='NYC' and r['tau_m']=='5000' and r['workload']=='medium' and r['variant']=='full'),(5,lambda r:r['region']=='NYC' and r['tau_m']=='5000' and r['rest_ef']=='1600' and r['variant'] in ['full','pivot']),(6,lambda r:r['region']=='NYC' and r['tau_m'] in ['1000','2000','3000','4000','5000'] and r['rest_ef']=='1600' and r['workload']=='medium' and r['variant'] in ['full','pivot'])]:
 rr=[r for r in perf if select(r)];write(f'Exp{number}_MARS_rounds.csv',rr);write(f'Exp{number}_MARS_summary.csv',summary(rr,['region','tau_m','variant','rest_ef','workload'],['QPS','Recall']))
road=[]
for r in perf:
 if r['variant']!='pointwise':continue
 f=next(x for x in perf if x['config_id']==r['config_id'].replace('_pointwise_','_full_') and x['repetition']==r['repetition'])
 road.append({'region':r['region'],'tau_m':r['tau_m'],'repetition':r['repetition'],'full_QPS':float(f['QPS']),'direct_QPS':float(r['QPS']),'paired_speedup':float(f['QPS'])/float(r['QPS']),'full_Recall':float(f['Recall']),'direct_Recall':float(r['Recall'])})
assert len(road)==24;write('Exp7_paired_road_rounds.csv',road);write('Exp7_paired_road_summary.csv',summary(road,['region','tau_m'],['full_QPS','direct_QPS','paired_speedup','full_Recall','direct_Recall']));write('Exp7_independent_diagnostics_summary.csv',rows(P/'outputs/diagnostic_summary.csv'))
oldcost=rows(next((S/'student_measure_entry_20261007_v3_exact_validation/outputs').glob('Exp8*逐轮*.csv')));cost=[r for r in oldcost if r['method']!='MARS'];assert len(cost)==48
for r in build:cost.append({**r,'method':'MARS','build_without_save_ms':r['additional_build_ms'],'serialized_bytes_total':r['serialized_bytes'],'payload_copy_bytes':r['explicit_payload_copy_bytes'],'structure_bytes_excluding_payload_copies':r['structure_bytes'],'build_peak_RSS_KiB':r['peak_RSS_KiB'],'structure_space_is_not_actual_disk_usage':True,'RSS_includes_shared_load':True,'raw_records':r['cost_source']})
write('Exp8_final_cost_rounds.csv',cost);write('Exp8_final_cost_summary.csv',summary(cost,['method','region'],['build_without_save_ms','save_ms','serialized_bytes_total','payload_copy_bytes','structure_bytes_excluding_payload_copies','build_peak_RSS_KiB']))
ar=json.loads((P/'outputs/alpha_parameter_rounds.json').read_text());gg=json.loads((P/'outputs/graph_only_rounds.json').read_text())
for r in ar:r['package_raw']=copy_raw(r['raw_file'],r['raw_sha256'],f"alpha_parameter/a{r['alpha']}_rep{r['repetition']}.csv")
for r in gg:r['package_raw']=copy_raw(r['raw_file'],r['raw_sha256'],f"graph_only/ef{r['ef']}_rep{r['repetition']}.csv")
write('Alpha_parameter_rounds.csv',ar);aps=summary(ar,['alpha'],['QPS','Recall']);write('Alpha_parameter_summary.csv',aps)
write('Graph_only_rounds.csv',gg);ggs=summary(gg,['ef'],['QPS','Recall','empty_results','under_k_returns']);write('Graph_only_summary.csv',ggs)
for dir in ['configs','queries','provenance']:
 shutil.copytree(P/dir,D/dir)
for file in ['FORMAL_FREEZE.json','MAIN_COMPLETE.json','ALPHA_PARAMETER_COMPLETE.json','GRAPH_COMPLETE.json','EXTENSIONS_COMPLETE.json']:shutil.copy2(P/file,D/file)
for r in build:
 dst=D/f"construction/MARS/{r['region']}_rep{r['repetition']}";shutil.copytree(Path(r['cost_source']),dst);shutil.copy2(r['index_binding'],dst/'index_binding.json')
for r in oldcost:
 if r['method']=='MARS':continue
 src=Path(r['raw_records']);dest=D/f"construction/baselines/{r['method']}_{r['region']}_rep{r['repetition']}";dest.mkdir(parents=True)
 for x in src.rglob('*'):
  if x.is_file() and x.suffix in ['.csv','.json','.tsv','.txt'] and x.stat().st_size<5_000_000:
   target=dest/x.relative_to(src);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(x,target)
for r in ar+gg:
 if 'binding_file' in r:
  p=Path(r['binding_file']);dest=D/'index_bindings'/('parameter_a'+str(r['alpha'])+'_rep'+str(r['repetition'])+'.json' if 'alpha' in r else 'graph_ef'+str(r['ef'])+'_rep'+str(r['repetition'])+'.json');dest.parent.mkdir(exist_ok=True);shutil.copy2(p,dest)
notes=[
'此包只采用频率划分 MARS，默认 alpha=64、ef=1600。类别版数据仅作为独立历史参照，不混入 MARS 均值。',
'主实验按同配置三个分别构建的索引实例计算均值和样本标准差；参数分析 alpha2 为已接受的早期专用实现，其他阈值为一般实现，须披露版本及测量时段差异。',
'Graph-only 每个类别ID一个图节点并保留全部对象关联，使用 CPU96/NUMA4。MARS 沿用未单核绑定的单线程设置；同ef1600对照不能称为完全相同CPU亲和性。',
'三组查询基线复用已完成36轮，其中10个历史运行窗口观察到外部进程；观测不能量化其对计时查询的影响，不能写成完全隔离机器测量。HNSW沿用修复后最终版本。',
'Exp8 构建额外成本包含频率归并、通道划分和关联映射；共享载入、保存、重载另列。完整序列化体积与排除明确payload副本的结构空间分开，后者不是实际磁盘占用。RSS在额外重载验收之前采集并含共享载入。',
'新索引先在原学生数据磁盘构建、保存和查询，完成后校验哈希并归档到获准访问的NAS学生目录；不将归档复制时间计入构建或查询性能。旧索引未移动。',
'论文修改只改受影响数字和不再成立的趋势：实验设置标明freq(x)>=64、pivot仅更新已处理方面；替换Exp1–8的MARS表格/曲线和加速倍数；三组全方法表、Graph-only、alpha参数图采用本包。原结构和无关文字保留。',
'道路编译加速采用同实例full/direct性能；诊断单独记录，不能用诊断耗时反推QPS。Recall仍用min(k,GT条数)分母及原语义1e-6容差，容差不扩大道路范围。',
'尚未自动修改任何论文、图表、默认代码仓库或旧正式结果。'
]
def table(rs,keys):return '<table><thead><tr>'+''.join('<th>'+html.escape(k)+'</th>' for k in keys)+'</tr></thead><tbody>'+''.join('<tr>'+''.join('<td>'+html.escape(f'{r[k]:.6g}' if isinstance(r.get(k),float) else str(r.get(k,'')))+'</td>' for k in keys)+'</tr>' for r in rs)+'</tbody></table>'
body='<meta charset="utf-8"><title>MARS alpha64 final results</title><style>body{font:16px Arial;max-width:1400px;margin:32px auto;line-height:1.65}table{border-collapse:collapse;margin:20px 0}th,td{border:1px solid #ccc;padding:6px 12px;text-align:right}th{background:#eef2f7}</style><h1>频率划分 MARS：α＝64 正式实验</h1>'
body+=''.join('<p>'+html.escape(n)+'</p>' for n in notes)
for title,rs,keys in [('Exp1 默认点及排名',rank,['region','method','QPS_mean','QPS_sample_sd','Recall_mean','Recall_sample_sd','throughput_rank','MARS_QPS_over_method']),('三组查询全部算法',gs,['method','workload','QPS_mean','QPS_sample_sd','Recall_mean','Recall_sample_sd']),('α 参数分析',sorted(aps,key=lambda x:int(x['alpha'])),['alpha','QPS_mean','QPS_sample_sd','Recall_mean','Recall_sample_sd']),('Graph-only',ggs,['ef','QPS_mean','QPS_sample_sd','Recall_mean','Recall_sample_sd','empty_results_mean','under_k_returns_mean'])]:body+='<h2>'+title+'</h2>'+table(rs,keys)
body+='<h2>全部绘图与逐轮数据</h2>'+''.join('<p><a href="'+p.name+'">'+p.name+'</a></p>' for p in D.glob('*.csv'))
(D/'正式结果总表与说明.html').write_text(body,encoding='utf-8');(D/'论文最小修改说明.txt').write_text('\n\n'.join(notes),encoding='utf-8')
audit={'status':'PASS','default_alpha':64,'main_MARS_performance':144,'main_MARS_diagnostic':48,'default_MARS_cost_instances':12,'main_all_method_rounds':540,'unchanged_non_MARS_main_rounds':468,'query_groups_all_method_rounds':45,'alpha_parameter_rounds':33,'alpha_points':11,'graph_rounds':9,'graph_instances':3,'means_and_sample_sd':True,'raw_hashes_verified':True,'category_MARS_excluded_from_final_MARS_statistics':True,'limitations':notes[1:4]};save(D/'FINAL_DELIVERY_AUDIT.json',audit)
manifest={str(p.relative_to(D)):{'bytes':p.stat().st_size,'sha256':sha(p)} for p in D.rglob('*') if p.is_file()};save(D/'DELIVERY_MANIFEST.json',manifest)
z=P/'MARS_alpha64_final_records.zip'
with zipfile.ZipFile(z,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as f:
 for p in D.rglob('*'):
  if p.is_file():f.write(p,'final_delivery_v1/'+str(p.relative_to(D)))
save(P/'DELIVERY_READY.json',{'zip':str(z),'bytes':z.stat().st_size,'sha256':sha(z),'report':str(D/'正式结果总表与说明.html'),'audit':audit});print('DELIVERY_READY',flush=True)
