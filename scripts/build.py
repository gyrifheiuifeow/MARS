#!/usr/bin/env python3
"""Compile the C++ experiment programs."""
from pathlib import Path
import argparse, hashlib, json, os, platform, subprocess

ROOT = Path(__file__).resolve().parents[1]
TARGETS = ("mars_perf", "mars_diag", "mars_accept", "baseline_entry", "frequency_reference")

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--target", choices=("all",) + TARGETS, default="all")
    p.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    a = p.parse_args()
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "amd64"):
        p.error("The frozen kernels target Linux x86-64 with AVX2/FMA and OpenMP.")
    out = ROOT / "build" / "bin"
    out.mkdir(parents=True, exist_ok=True)
    records = []
    for target in TARGETS if a.target == "all" else (a.target,):
        runtime = ROOT / "runtime" / ("baselines" if target == "baseline_entry" else "mars")
        src = runtime / "src_tree"
        command = [a.cxx, "-std=c++17", "-O3", "-mtune=native", "-mavx2", "-mfma", "-fopenmp", "-mcmodel=large"]
        command += [f"-DSTUDENT_DIAGNOSTICS={int(target == 'mars_diag')}", f"-DSTUDENT_FREQ_ACCEPTANCE={int(target == 'mars_accept')}"]
        command += ["-I" + str(src / "baselines/mars_baselines"), "-I" + str(src / "baselines/RaBitQ-Library-main/include"), "-I" + str(src / "code")]
        name = "baseline_measure_entry.cpp" if target == "baseline_entry" else "frequency_reference.cpp" if target == "frequency_reference" else "freq_mars_entry.cpp"
        command += [str(runtime / "drivers" / name)]
        if target != "frequency_reference":
            command += [str(src / "code/h2h.cpp")]
        command += ["-o", str(out / target), "-lrt"]
        print("Compiling", target, flush=True)
        subprocess.run(command, check=True)
        records.append({"target": target, "argv": command, "sha256": hashlib.sha256((out / target).read_bytes()).hexdigest()})
    (ROOT / "build" / "build_manifest.json").write_text(json.dumps(records, indent=2) + "\n")

if __name__ == "__main__":
    main()
