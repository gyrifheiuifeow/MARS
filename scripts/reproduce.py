#!/usr/bin/env python3
"""Prepare portable paths or run one explicitly selected experiment stage."""
from pathlib import Path
import argparse, csv, hashlib, json, os, subprocess

ROOT = Path(__file__).resolve().parents[1]
REGIONS = ("NYC", "BAY", "COL", "FLA")
METHODS = ("MARS", "HNSW", "SymphonyQG", "Mesh-Road", "RoadScan")

def read_rows(path, delimiter=","):
    with Path(path).open(encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f, delimiter=delimiter))

def save_text(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text() != text:
        raise RuntimeError(f"Refusing to replace different prepared input: {path}")
    if not path.exists():
        path.write_text(text, encoding="utf-8")

def save_rows(path, rows, delimiter="\t"):
    import io
    f = io.StringIO(newline="")
    w = csv.DictWriter(f, fieldnames=list(rows[0]), delimiter=delimiter, lineterminator="\n")
    w.writeheader(); w.writerows(rows)
    save_text(path, f.getvalue())

def gt_path(data, original, region):
    specified = data / original
    alternative = data / "data" / region / "groundtruth" / specified.name
    return alternative if alternative.is_file() else specified

def prepare(data, output, alpha=64):
    data, output = Path(data).resolve(), Path(output).resolve()
    if any(c.isspace() for c in str(data)):
        raise ValueError("The frozen road manifest requires a data-root path without whitespace.")
    matrix = json.loads((ROOT / "configs/mars/performance_matrix.json").read_text())
    diag_ids = set(json.loads((ROOT / "configs/mars/diagnostic_config_ids.json").read_text()))
    assert len(matrix) == 48 and len(diag_ids) == 16
    for region in REGIONS:
        tasks = []
        for c in matrix:
            if c["region"] == region:
                assert c["alpha"] == 64 and c["k"] == 10
                tasks.append(dict(config_id=c["config_id"], tau=c["tau_m"], k=10, variant=c["variant"], rest_ef=c["rest_ef"], query_manifest=str(ROOT / c["query_manifest"]), workload=c["workload"], alpha=alpha))
        save_rows(output / "prepared" / f"MARS_{region}_perf.tsv", tasks)
        save_rows(output / "prepared" / f"MARS_{region}_diag.tsv", [t for t in tasks if t["config_id"] in diag_ids])
        accept_file = ROOT / "configs/mars/queries" / f"accept_{region}.csv"
        picks = read_rows(accept_file) if accept_file.exists() else read_rows(ROOT / "configs/mars/queries" / f"{region}_medium.csv")[:10]
        accept_copy = output / "prepared" / f"accept_{region}.csv"
        save_rows(accept_copy, picks, ",")
        save_rows(output / "prepared" / f"MARS_{region}_accept.tsv", [dict(config_id=f"accept_{region}_{v}", tau=6000, k=10, variant=v, rest_ef=1600, query_manifest=str(accept_copy), workload="acceptance_fixed", alpha=alpha) for v in ("full", "pivot", "pointwise")])
        for method in METHODS[1:]:
            rr = read_rows(ROOT / "configs/baselines" / f"{method}_{region}.tsv", "\t")
            for r in rr:
                r["GT_path"] = str(gt_path(data, r["GT_path"], region))
            save_rows(output / "prepared" / f"{method}_{region}.tsv", rr)
    units = read_rows(ROOT / "configs/road_units.tsv", "\t")
    for u in units:
        u["graph_path"] = str(data / u["graph_path"])
    save_rows(output / "prepared/road_units.tsv", units)
    save_text(output / "prepared/roots.json", json.dumps({"data_root": str(data), "output_root": str(output), "alpha": alpha, "performance_configs": 48, "performance_rounds": 144, "diagnostic_rounds": 48}, indent=2) + "\n")

def environment(data, output, alpha=64):
    e = dict(os.environ)
    e.update(OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1", MKL_NUM_THREADS="1", MARS_ROOT=str(data), MARS_OUTPUT_ROOT=str(output), MARS_REFERENCE_ROOT=str(output / "reference"), STUDENT_FREQ_ALPHA="64", MARS_CAT_SCORE_MODE="channel", MARS_CAT_PIVOT="1", MARS_CAT_UNIQUE="1", MARS_CAT_UNIQUE_EF="1600", MARS_CAT_POINTWISE_ROAD="0", ROAD_QSCAN_MODE="estimate_only", STUDENT_QSCAN_REUSE="0", STUDENT_HNSW_EMPTY_RESTART="1", EXACT_ROAD_UNITS_MANIFEST=str(output / "prepared/road_units.tsv"))
    e['STUDENT_FREQ_ALPHA']=str(alpha)
    return e

def command(method, region, rep, stage, data, output, index=None, alpha=64):
    if method != "MARS" and stage in ("diag", "reference", "partition"):
        raise ValueError("Independent phase diagnostics/reference export apply to MARS only.")
    instance = Path(index).resolve() if index else output / "instances" / method / region / f"rep{rep}"
    dest = output / "results" / method / region / f"rep{rep}" / stage
    if stage == "build" and index:
        raise ValueError("--index is read-only reuse; it cannot be supplied for a build.")
    if method == "MARS":
        if stage == "reference":
            return [str(ROOT / "build/bin/frequency_reference"), str(data / "data" / region), region, "reference", str(output / "reference" / f"{region}_independent.bin")], dest
        binary = "mars_diag" if stage == "diag" else "mars_accept" if stage == "accept" else "mars_perf"
        tasks = "-" if stage in ("build", "partition") else str(output / "prepared" / f"MARS_{region}_{stage}.tsv")
        return [str(ROOT / "build/bin" / binary), stage, region, str(instance), str(dest), tasks, str(alpha)], dest
    tasks = output / "prepared" / f"{method}_{region}.tsv"
    queries = ROOT / "configs/baselines/queries" / f"{region}_formal.csv"
    if stage == "accept":
        queries = output / "prepared" / f"{method}_{region}_accept.csv"
        save_rows(queries, read_rows(ROOT / "configs/baselines/queries" / f"{region}_formal.csv")[:20], ",")
    seed = 0 if method == "RoadScan" else 99 + rep
    args = [str(ROOT / "build/bin/baseline_entry"), "load" if stage == "perf" else stage, method, region, str(instance), str(dest), str(tasks), str(queries), str(seed), "reuse_off"]
    if method == "Mesh-Road":
        args.append(str(ROOT / "configs/baselines" / f"Mesh_{region}_placement.json"))
    return args, dest

def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="action", required=True)
    for name in ("prepare", "run"):
        s = sub.add_parser(name)
        s.add_argument("--data-root", type=Path, required=True, help="Directory containing data/<region> and optionally legacy groundtruth directories")
        s.add_argument("--output-root", type=Path, required=True, help="Fresh isolated output directory")
        s.add_argument("--alpha",type=int,default=64,choices=(2,4,8,16,32,64,96,128,256,512,1024),help="Frequency threshold; use a separate output root for each threshold. Historical alpha2 used a specialized implementation.")
    r = sub.choices["run"]
    r.add_argument("--method", choices=METHODS, required=True); r.add_argument("--region", choices=REGIONS, required=True)
    r.add_argument("--rep", type=int, choices=(1,2,3), default=1)
    r.add_argument("--stage", choices=("build", "perf", "diag", "accept", "reference", "partition"), required=True)
    r.add_argument("--index", type=Path); r.add_argument("--dry-run", action="store_true")
    a = p.parse_args(); data = a.data_root.resolve(); out = a.output_root.resolve()
    prepare(data, out, a.alpha)
    if a.action == "prepare":
        print("Prepared frozen task paths; no search or index build executed."); return
    args, dest = command(a.method, a.region, a.rep, a.stage, data, out, a.index, a.alpha)
    print(json.dumps({"argv": args, "alpha":a.alpha if a.method == "MARS" else None, "single_thread":True}, indent=2))
    if a.dry_run: return
    if dest.exists(): raise RuntimeError(f"Refusing to overwrite an existing stage, including failed output: {dest}")
    if not Path(args[0]).is_file(): raise RuntimeError("Build executable first with scripts/build.py")
    (out / "reference").mkdir(parents=True, exist_ok=True)
    env = environment(data, out, a.alpha); env["EXACT_ROAD_REGION"] = a.region
    env["EXACT_ROAD_ASSERTION_LOG"] = str(dest / "extra_exact_road_checks.tsv")
    logs=out/"logs";logs.mkdir(parents=True,exist_ok=True);stem=f"{a.method}_{a.region}_{a.rep}_{a.stage}"
    with (logs/(stem+".command.json")).open("x") as f:json.dump({"argv":args,"environment_overrides":{k:env[k] for k in ("MARS_ROOT","MARS_OUTPUT_ROOT","STUDENT_FREQ_ALPHA","STUDENT_HNSW_EMPTY_RESTART","STUDENT_QSCAN_REUSE","OMP_NUM_THREADS")}},f,indent=2)
    with (logs/(stem+".log")).open("x") as log:
        subprocess.run(args, env=env, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    print("Stage completed. Preserve outputs and index files for reuse.")

if __name__ == "__main__":
    main()
