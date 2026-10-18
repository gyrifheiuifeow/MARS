from pathlib import Path
import json,os,shutil
S=Path('/workspace/MARSExp2/submission_experiments_20260726/results/student_runs')
for sub,rels in {
'student_mars_freq_general_a64_pilot_20261015_v1':['runs/NYC/ACCEPTANCE_PASS.json','comparison_full1000_20261015_v1/outputs/new_performance_results.json','medium6_full1000_20261016_v1/outputs/new_results.json','configs/environment.json'],
'student_exp5_baselines_query_groups_20261015_v2/execution_v4':['FINAL_DELIVERY_AUDIT_20261015.json','COMPLETED.json'],
'student_graph_only_shared_category_nyc_20261016_v1':['configs/actual_configuration.json','configs/environment.json','SHARED_GRAPH_FREEZE.json'],
}.items():
 for rel in rels:
  p=S/sub/rel
  print('\nFILE',p,'\n',p.read_text()[:11000] if p.exists() else 'MISSING')
for sub in ['student_graph_only_shared_category_nyc_ef3200_6400_20261016_v1','student_graph_only_shared_category_nyc_medium1000_20261016_v1','student_mars_freq_general_a64_pilot_20261015_v1/medium6_full1000_20261016_v1','student_exp5_baselines_query_groups_20261015_v2/execution_v4/outputs']:
 print('FILES',sub,[str(x.relative_to(S/sub)) for x in (S/sub).glob('*')])
nas=Path('/mnt/nas_mount');print('NAS',shutil.disk_usage(nas),os.access(nas,os.W_OK))
print('source sample')
p=S/'student_graph_only_shared_category_nyc_20261016_v1/runtime'
print([str(x.relative_to(p)) for x in p.glob('*')])
