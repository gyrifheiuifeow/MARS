#!/usr/bin/env python3
"""Prepare or run the MARS experiment matrix serially."""
from pathlib import Path
import argparse,json,subprocess,sys,hashlib,csv
from reproduce import ROOT,REGIONS,prepare

def sha(p):
    h=hashlib.sha256()
    with p.open("rb") as f:
        for b in iter(lambda:f.read(8*1024*1024),b""):h.update(b)
    return h.hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--data-root",type=Path,required=True);p.add_argument("--output-root",type=Path,required=True)
    p.add_argument("--reuse-manifest",type=Path)
    p.add_argument("--execute",action="store_true",help="Without this option, print a plan only")
    a=p.parse_args();out=a.output_root.resolve();data=a.data_root.resolve();prepare(data,out)
    reused={}
    if a.reuse_manifest:
        for r in json.loads(a.reuse_manifest.read_text()):
            key=(r["region"],int(r["repetition"]));assert key not in reused and key[0] in REGIONS and key[1] in [1,2,3];reused[key]=r
    plan={"alpha":64,"unique_performance_configs":48,"performance_rounds":144,"diagnostic_rounds":48,"queries_each":1000,"instances":12,"reused_instances":len(reused),"new_builds":12-len(reused),"serial":True}
    print(json.dumps(plan,indent=2))
    if not a.execute:return
    from verify_data import check
    check(data)
    marker=out/"MATRIX_STARTED.json"
    if marker.exists():raise RuntimeError("Existing execution: inspect saved results; do not automatically rerun.")
    marker.write_text(json.dumps(plan,indent=2)+"\n")
    if reused:(out/"REUSED_INDICES.json").write_text(json.dumps(list(reused.values()),indent=2)+"\n")
    def run(region,rep,stage,index=None):
        cmd=[sys.executable,str(ROOT/"scripts/reproduce.py"),"run","--method","MARS","--region",region,"--rep",str(rep),"--stage",stage,"--data-root",str(data),"--output-root",str(out)]
        if index:cmd += ["--index",str(index)]
        subprocess.run(cmd,check=True)
    def check_reuse(record):
        cost=Path(record["cost_records"]);instance=Path(record["instance"])
        assert set(record["files"]) >= {"frequency_state.bin","mars_aux.bin","rest_graph.bin","identity.json"}
        for filename in ["build_stages.csv","space.csv","build_memory.json"]:assert (cost/filename).is_file(),filename
        memory=json.loads((cost/"build_memory.json").read_text())
        assert memory["includes_shared_input_load"] and not memory["includes_validation_or_evaluation_queries"]
        identity=json.loads((instance/"identity.json").read_text())
        assert identity["alpha"]==64 and identity["region"]==record["region"]
        with (cost/"space.csv").open(newline="") as f:
            for row in csv.DictReader(f):
                assert int(row["serialized_bytes"])-int(row["embedded_shared_payload_bytes"])==int(row["additional_bytes"])
                assert (instance/Path(row["file"]).name).stat().st_size==int(row["serialized_bytes"])
        for name,h in record["files"].items():assert sha(Path(record["instance"])/name)==h,name
    for region in REGIONS:
        record=reused.get((region,1));index=Path(record["instance"]) if record else out/"instances/MARS"/region/"rep1"
        if record:check_reuse(record)
        else:run(region,1,"build")
        run(region,1,"reference");run(region,1,"partition")
        partition=out/"results/MARS"/region/"rep1/partition"
        assert sha(partition/"canonical.bin")==sha(out/"reference"/f"{region}_independent.bin")
        assert sha(partition/"frequency_state.bin")==sha(index/"frequency_state.bin")
        run(region,1,"accept",index if record else None)
    for region in REGIONS:
        for rep in (1,2,3):
            record=reused.get((region,rep));index=Path(record["instance"]) if record else None
            if rep>1:
                if record:check_reuse(record)
                else:run(region,rep,"build")
            run(region,rep,"perf",index);run(region,rep,"diag",index)
    subprocess.run([sys.executable,str(ROOT/"scripts/validate_outputs.py"),"--output-root",str(out),"--require-complete"],check=True)
    subprocess.run([sys.executable,str(ROOT/"scripts/summarize.py"),"--output-root",str(out)],check=True)
    (out/"MATRIX_COMPLETE.json").write_text(json.dumps(plan,indent=2)+"\n")

if __name__=="__main__":main()
