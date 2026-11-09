#!/usr/bin/env python3
"""Summarize the recorded query results."""
from pathlib import Path
import argparse,csv,json,statistics as st,collections,math

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--output-root",type=Path,required=True);a=p.parse_args()
    root=a.output_root.resolve();destination=root/"summary";destination.mkdir(exist_ok=False);rounds=[]
    for methoddir in (root/"results").iterdir():
        if not methoddir.is_dir():continue
        for regiondir in methoddir.iterdir():
            for repdir in regiondir.glob("rep[123]"):
                for mode in ("perf","diag"):
                    stage=repdir/mode
                    paths=list(stage.glob("raw/*.csv")) if methoddir.name=="MARS" else list(stage.glob("*.csv"))
                    for path in paths:
                        with path.open(newline="") as f:rr=list(csv.DictReader(f))
                        if not rr or "core_query_ms" not in rr[0]:continue
                        key="score_recall" if "score_recall" in rr[0] else "Recall"
                        assert all(math.isfinite(float(r["core_query_ms"])) and float(r["core_query_ms"])>0 for r in rr)
                        item=dict(method=methoddir.name,region=regiondir.name,repetition=int(repdir.name[3:]),mode=mode,config_id=path.stem,queries=len(rr),Recall=st.mean(float(r[key]) for r in rr),mean_query_ms=st.mean(float(r["core_query_ms"]) for r in rr),source=str(path))
                        item["QPS"]=1000/item["mean_query_ms"] if mode=="perf" else None;rounds.append(item)
    groups=collections.defaultdict(list)
    for r in rounds:groups[r["method"],r["region"],r["mode"],r["config_id"]].append(r)
    summary=[]
    for k,rr in sorted(groups.items()):
        assert len({r["repetition"] for r in rr})==len(rr)<=3
        row=dict(zip(("method","region","mode","config_id"),k));row["instances"]=len(rr)
        for metric in (["QPS"] if k[2]=="perf" else [])+["Recall","mean_query_ms"]:
            values=[r[metric] for r in rr];row[metric+"_mean"]=st.mean(values);row[metric+"_sample_sd"]=st.stdev(values) if len(values)>1 else None
        summary.append(row)
    for name,data in (("rounds",rounds),("summary",summary)):
        (destination/(name+".json")).write_text(json.dumps(data,indent=2)+"\n")
        if data:
            with (destination/(name+".csv")).open("w",newline="") as f:
                fields=list(dict.fromkeys(k for r in data for k in r));w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(data)
    costs=[]
    def add_cost(folder,method,region,rep,reused):
        with (folder/"build_stages.csv").open(newline="") as f:stages={r["phase"]:float(r["exclusive_ms"]) for r in csv.DictReader(f)}
        with (folder/"space.csv").open(newline="") as f:space=list(csv.DictReader(f))
        memory=json.loads((folder/"build_memory.json").read_text());assert not memory["includes_validation_or_evaluation_queries"]
        costs.append(dict(method=method,region=region,repetition=rep,reused=reused,shared_load_ms=stages["shared_load"],method_preprocess_ms=stages["method_preprocess"],structure_build_ms=stages["structure_build"],additional_build_ms=stages["method_preprocess"]+stages["structure_build"],save_ms=stages["save"],serialized_bytes=sum(int(x["serialized_bytes"]) for x in space),structural_bytes_excluding_payload=sum(int(x["additional_bytes"]) for x in space),build_peak_RSS_KiB=memory["process_peak_rss_kib_before_queries"],source=str(folder)))
    for path in (root/"results").glob("*/*/rep[123]/build/build_stages.csv"):
        method,region,rep=path.relative_to(root/"results").parts[:3];add_cost(path.parent,method,region,int(rep[3:]),False)
    if (root/"REUSED_INDICES.json").is_file():
        for r in json.loads((root/"REUSED_INDICES.json").read_text()):add_cost(Path(r["cost_records"]),"MARS",r["region"],r["repetition"],True)
    (destination/"construction_rounds.json").write_text(json.dumps(costs,indent=2)+"\n")
    if costs:
        with (destination/"construction_rounds.csv").open("w",newline="") as f:w=csv.DictWriter(f,fieldnames=list(costs[0]));w.writeheader();w.writerows(costs)
    print(f"Summarized {len(rounds)} query runs and {len(costs)} construction records. Missing instances remain explicitly incomplete.")

if __name__=="__main__":main()
