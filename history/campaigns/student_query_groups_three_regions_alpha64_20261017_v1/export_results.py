from pathlib import Path
import json,csv,statistics as st,hashlib,shutil,html,zipfile
P=Path(__file__).resolve().parent;S=P.parent;D=P/'delivery_v1';D.mkdir(exist_ok=False)
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def read(p):return list(csv.DictReader(Path(p).open(encoding='utf-8-sig')))
def save(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2),encoding='utf-8')
def write(name,rs):
 keys=list(dict.fromkeys(k for r in rs for k in r))
 with (D/name).open('x',encoding='utf-8-sig',newline='') as f:w=csv.DictWriter(f,fieldnames=keys);w.writeheader();w.writerows(rs)
new=json.loads((P/'outputs/per_round.json').read_text());assert len(new)==135
A=S/'student_mars_alpha64_final_20261017_v1';B=S/'student_exp5_baselines_query_groups_20261015_v2/execution_v4';allrows=[]
for r in new:allrows.append({**r,'source_version':'new_three_regions_CPU96_NUMA4','reused':False})
for r in read(A/'outputs/performance_rounds.csv'):
 if r['region']=='NYC' and r['tau_m']=='5000' and r['rest_ef']=='1600' and r['variant']=='full':allrows.append({'method':'MARS','region':'NYC','group':r['workload'],'rep':int(r['repetition']),'tau_m':5000,'k':10,'queries':1000,'QPS':float(r['QPS']),'Recall':float(r['Recall']),'empty_results':int(r['empty_results']),'raw_file':r['raw_file'],'raw_sha256':r['raw_sha256'],'index_binding':r['index_binding'],'CPU':None,'NUMA':None,'source_version':'qualified_alpha64_NYC_previous_formal','reused':True})
for r in read(B/'outputs/per_round.csv'):allrows.append({'method':r['method'],'region':'NYC','group':r['group'],'rep':int(r['rep']),'tau_m':5000,'k':10,'queries':1000,'QPS':float(r['QPS']),'Recall':float(r['Recall_audit']),'empty_results':int(r['empty_results']),'raw_file':r['raw_file'],'raw_sha256':r['raw_sha256'],'CPU':None,'NUMA':None,'source_version':'qualified_NYC_group_baselines_previous','reused':True,'external_activity_observed':r['external_activity_observed']})
assert len(allrows)==180;assert len({(r['region'],r['method'],r['group'],r['rep']) for r in allrows})==180
summary=[];regions=['NYC','BAY','COL','FLA'];methods=['MARS','RoadScan','HNSW','SymphonyQG','Mesh-Road'];groups=['popular','medium','rare']
for reg in regions:
 for method in methods:
  for g in groups:
   rs=[r for r in allrows if r['region']==reg and r['method']==method and r['group']==g];assert {r['rep'] for r in rs}=={1,2,3}
   row={'region':reg,'method':method,'query_group':g,'tau_m':5000,'k':10,'alpha':64 if method=='MARS' else None,'rest_ef':1600 if method=='MARS' else None,'instances':3,'queries_per_instance':1000}
   for metric in ['QPS','Recall']:row[metric+'_mean']=st.mean(r[metric] for r in rs);row[metric+'_sample_sd']=st.stdev(r[metric] for r in rs)
   row['Recall_percent_mean']=100*row['Recall_mean'];row['Recall_percent_sample_sd']=100*row['Recall_sample_sd'];summary.append(row)
for r in allrows:
 assert sha(r['raw_file'])==r['raw_sha256'];dest=D/f"raw/{r['region']}/{r['method']}_{r['group']}_rep{r['rep']}.csv";dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(r['raw_file'],dest);r['package_raw']=str(dest.relative_to(D))
write('all_four_regions_summary.csv',summary);write('all_four_regions_rounds.csv',allrows)
for metric in ['QPS_mean','QPS_sample_sd','Recall_mean','Recall_sample_sd']:
 write(metric+'_wide.csv',[{'region':reg,'method':method,**{g:next(r[metric] for r in summary if r['region']==reg and r['method']==method and r['query_group']==g) for g in groups}} for reg in regions for method in methods])
for d in ['configs','queries','bindings','provenance','groundtruth','gt_validation']:shutil.copytree(P/d,D/d)
for f in ['EXPERIMENT_FREEZE.json','APPROVED_PLAN.json','MEASUREMENTS_COMPLETE.json']:shutil.copy2(P/f,D/f)
shutil.copy2(B/'FINAL_DELIVERY_AUDIT_20261015.json',D/'NYC_historical_baseline_audit.json')
notes='四区均固定medium/popular/rare三组原完整1000查询、5km、k10。MARS alpha64、ef1600；HNSW修复版；RoadScan评分复用关闭。NYC45轮旧合格结果复用，三区新增135轮，索引全部复用，没有建图。新三区使用CPU96、严格NUMA4，串行单线程；NYC旧记录没有同样统一绑核，且36轮基线的10个运行窗口曾观察到外部进程。因此不能称全部四区为同期完全隔离的严格对照。三实例均值与样本标准差，Recall为0至1小数，百分比列另提供。样本标准差不是置信区间。\n三区popular/rare原5km旧GT对应不同查询向量，未复用；新目录用原语义评分和对象记录生成六份准确对应的GT，严格整数道路距离<=5000米，未使用epsilon扩大道路范围；先与既有medium GT固定查询的结果数量与分数核验。旧GT和旧结果未修改。\n'
(D/'README_说明.txt').write_text(notes,encoding='utf-8')
h='<!doctype html><meta charset="utf-8"><title>四区三组查询全部算法</title><style>body{font:16px Arial;line-height:1.7;max-width:1250px;margin:30px auto;padding:0 20px}table{width:100%;border-collapse:collapse}th,td{padding:8px;border-bottom:1px solid #ddd;text-align:right}th{background:#eef2f7}td:first-child{text-align:left}.mars{background:#eef7ed}</style><h1>四区三组查询：全部算法正式结果</h1><p>'+html.escape(notes).replace('\n','<br>')+'</p><p><a href="all_four_regions_summary.csv">全部绘图长表</a> · <a href="all_four_regions_rounds.csv">逐轮来源</a></p>'
for reg in regions:
 h+='<h2>'+reg+'</h2>'
 for metric in ['QPS','Recall']:
  h+='<p>'+metric+('（%）' if metric=='Recall' else '')+'</p><table><tr><th>算法</th>'+''.join('<th>'+g+'</th>' for g in groups)+'</tr>'
  for method in methods:
   h+='<tr'+(' class="mars"' if method=='MARS' else '')+'><td>'+method+'</td>'
   for g in groups:
    r=next(r for r in summary if r['region']==reg and r['method']==method and r['query_group']==g);scale=100 if metric=='Recall' else 1;h+=f"<td>{r[metric+'_mean']*scale:.3f} ± {r[metric+'_sample_sd']*scale:.3f}</td>"
   h+='</tr>'
  h+='</table>'
(D/'四区三组查询全部算法汇总.html').write_text(h,encoding='utf-8')
save(D/'FINAL_AUDIT.json',{'status':'PASS','settings':60,'rounds':180,'new_rounds':135,'NYC_reused_rounds':45,'new_indices':0,'new_GT_files':6,'all_three_instances':True,'raw_hashes_verified':True,'statistics':'mean and sample standard deviation','CPU96_NUMA4_new_regions':True,'NYC_execution_policy_difference_disclosed':True})
save(D/'MANIFEST.json',{str(f.relative_to(D)):{'bytes':f.stat().st_size,'sha256':sha(f)} for f in D.rglob('*') if f.is_file()})
z=P/'four_regions_query_groups_alpha64.zip'
with zipfile.ZipFile(z,'x',zipfile.ZIP_DEFLATED) as f:
 for p in D.rglob('*'):
  if p.is_file():f.write(p,'delivery_v1/'+str(p.relative_to(D)))
save(P/'DELIVERY_READY.json',{'zip':str(z),'bytes':z.stat().st_size,'sha256':sha(z),'report':str(D/'四区三组查询全部算法汇总.html')});print('DELIVERY_READY',flush=True)
