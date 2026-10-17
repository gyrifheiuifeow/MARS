import importlib.util,json,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("reproduce",ROOT/"scripts/reproduce.py");m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class ManifestTests(unittest.TestCase):
    def test_parameter_threshold_is_bound_and_cannot_mix_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            data=Path(tmp)/"data";out=Path(tmp)/"alpha128"
            m.prepare(data,out,128)
            self.assertTrue(all(r["alpha"]=="128" for r in m.read_rows(out/"prepared/MARS_NYC_perf.tsv","\t")))
            self.assertEqual(m.environment(data,out,128)["STUDENT_FREQ_ALPHA"],"128")
            args,_=m.command("MARS","NYC",1,"build",data,out,alpha=128)
            self.assertEqual(args[-1],"128")
            with self.assertRaises(RuntimeError):m.prepare(data,out,64)
    def test_matrix_and_full_workloads(self):
        configs=json.loads((ROOT/"configs/mars/performance_matrix.json").read_text())
        self.assertEqual(len({r["config_id"] for r in configs}),48)
        self.assertEqual(len(json.loads((ROOT/"configs/mars/diagnostic_config_ids.json").read_text())),16)
        for c in configs:
            self.assertEqual(c["alpha"],64);self.assertEqual(c["k"],10)
            rr=m.read_rows(ROOT/c["query_manifest"])
            self.assertEqual(len(rr),1000);self.assertEqual(len({(r["query_tag"],r["query_id"]) for r in rr}),1000)

    def test_portable_paths_and_final_switches(self):
        with tempfile.TemporaryDirectory() as tmp:
            data=Path(tmp)/"dataset";out=Path(tmp)/"run";m.prepare(data,out)
            env=m.environment(data,out)
            self.assertEqual(env["STUDENT_HNSW_EMPTY_RESTART"],"1")
            self.assertEqual(env["STUDENT_QSCAN_REUSE"],"0")
            self.assertEqual(env["OMP_NUM_THREADS"],"1")
            tasks=m.read_rows(out/"prepared/MARS_NYC_perf.tsv","\t")
            self.assertEqual(len(tasks),27);self.assertTrue(all(r["alpha"]=="64" for r in tasks))
            args,_=m.command("HNSW","NYC",2,"perf",data,out)
            self.assertEqual(args[-2:], ["101","reuse_off"])
            with self.assertRaises(ValueError):m.command("MARS","NYC",1,"build",data,out,Path("existing-index"))

if __name__=="__main__":unittest.main()
