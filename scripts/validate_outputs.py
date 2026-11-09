#!/usr/bin/env python3
"""Check query coverage and result consistency."""
from pathlib import Path
import argparse,csv,json,math
ROOT=Path(__file__).resolve().parents[1]
def read(p):
    with p.open(newline='') as f:return list(csv.DictReader(f))
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output-root',type=Path,required=True);p.add_argument('--require-complete',action='store_true');a=p.parse_args()
    root=a.output_root.resolve();configs={r['config_id']:r for r in json.loads((ROOT/'configs/mars/performance_matrix.json').read_text())};diag_ids=set(json.loads((ROOT/'configs/mars/diagnostic_config_ids.json').read_text()));cache={}
    fields=['query_tag','query_id','row_index','source_vertex','result_ids','exact_scores','score_recall','id_recall','GT_result_count','recall_denominator']
    for path in (root/'results/MARS').glob('*/rep[123]/*/raw/*.csv'):
        region,rep,mode=path.relative_to(root/'results/MARS').parts[:3]
        if mode not in ['perf','diag']:continue
        c=configs[path.stem];rr=read(path);qq=read(ROOT/c['query_manifest']);assert len(rr)==len(qq)==1000
        assert c['region']==region and (mode!='diag' or path.stem in diag_ids)
        for r,q in zip(rr,qq):
            assert all(r[k]==q[k] for k in ['query_tag','query_id','row_index','source_vertex'])
            assert int(r['alpha'])==64 and int(r['k'])==10 and int(r['rest_ef'])==c['rest_ef'] and int(r['tau_m'])==c['tau_m']
            assert r['variant']==c['variant'] and r['workload']==c['workload']
            assert int(r['recall_denominator'])==min(10,int(r['GT_result_count']))
            ids=r['result_ids'].split(';') if r['result_ids'] else [];assert len(ids)==len(set(ids))==int(r['returned_objects'])<=int(r['recall_denominator'])
            assert math.isfinite(float(r['core_query_ms'])) and float(r['core_query_ms'])>0 and 0<=float(r['score_recall'])<=1
            if mode=='diag':
                assert abs(float(r['phase_error_ms']))<1e-8
                assert int(r['h2h_core_total'])==sum(int(r[k]) for k in ['h2h_init','h2h_compile','h2h_pivot','h2h_rest','h2h_result','h2h_cleanup'])
                assert int(r['posting_scan_total'])==sum(int(r[k]) for k in ['posting_direct','posting_interval','posting_fringe'])
        cache[region,rep,mode,path.stem]=[tuple(r[k] for k in fields) for r in rr]
    if a.require_complete:
        expected={(c['region'],f'rep{i}','perf',cid) for cid,c in configs.items() for i in [1,2,3]}
        expected|={(configs[cid]['region'],f'rep{i}','diag',cid) for cid in diag_ids for i in [1,2,3]}
        assert set(cache)==expected,'Incomplete or unexpected formal coverage'
    for (region,rep,mode,cid),rr in cache.items():
        if '_pointwise_' in cid:
            other=(region,rep,mode,cid.replace('_pointwise_','_full_'))
            if a.require_complete or other in cache:assert rr==cache[other]
        if mode=='diag':
            other=(region,rep,'perf',cid)
            if a.require_complete or other in cache:assert rr==cache[other]
    result={'validated_query_runs':len(cache),'full_matrix_required':a.require_complete,'performance_runs':sum(k[2]=='perf' for k in cache),'diagnostic_runs':sum(k[2]=='diag' for k in cache)}
    (root/'OUTPUT_VALIDATION.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
