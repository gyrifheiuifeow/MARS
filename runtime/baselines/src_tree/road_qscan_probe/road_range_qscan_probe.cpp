#include <cstdint>
#ifndef STUDENT_DIAGNOSTICS
#define STUDENT_DIAGNOSTICS 0
#endif
inline uint64_t student_qscan_calls=0;
inline bool student_count_calls=false;
#include "../baselines/mars_baselines/baseline_common.hpp"

struct QuantizedVector {
    vector<uint8_t> codes;
    float error = 0.0f;
};

struct QuantizedStore {
    FvecsReader& reader;
    int bits = 5;
    int dim = 0;
    int levels = 31;
    double delta = 0.0;
    vector<QuantizedVector> rows;

    QuantizedStore(FvecsReader& r, int b) : reader(r), bits(max(1, min(8, b))), dim(r.dim()) {
        levels = (1 << bits) - 1;
        delta = 2.0 / double(levels);
        rows.resize(size_t(reader.rows()) + 1);
    }

    void buildAll() {
        for (int id = 1; id <= reader.rows(); ++id) {
            quantize(id);
        }
    }

    const QuantizedVector& get(int id) {
        if (id <= 0 || id >= static_cast<int>(rows.size())) throw runtime_error("quantized id out of range");
        if (rows[id].codes.empty()) quantize(id);
        return rows[id];
    }

    double indexMB() const {
        double bytes = 0.0;
        for (const auto& row : rows) {
            bytes += double(row.codes.size()) * sizeof(uint8_t);
            if (!row.codes.empty()) bytes += sizeof(float);
        }
        return bytes / (1024.0 * 1024.0);
    }

   private:
    void quantize(int id) {
        vector<float> v = reader.readNorm1Based(id);
        QuantizedVector qv;
        qv.codes.resize(dim);
        double err2 = 0.0;
        for (int d = 0; d < dim; ++d) {
            double clipped = max(-1.0, min(1.0, double(v[d])));
            int code = int(llround((clipped + 1.0) / delta));
            code = max(0, min(levels, code));
            qv.codes[d] = static_cast<uint8_t>(code);
            double recon = double(code) * delta - 1.0;
            double err = double(v[d]) - recon;
            err2 += err * err;
        }
        qv.error = static_cast<float>(sqrt(err2));
        rows[id] = std::move(qv);
    }
};

struct QueryWeights {
    vector<double> scaled;
    double qsum = 0.0;
};

static QueryWeights makeWeights(const vector<float>& q, double delta) {
    QueryWeights w;
    w.scaled.resize(q.size());
    for (size_t i = 0; i < q.size(); ++i) {
        w.scaled[i] = double(q[i]) * delta;
        w.qsum += double(q[i]);
    }
    return w;
}

static double quantizedEstimate(const QuantizedVector& v, const QueryWeights& w) {
#if STUDENT_DIAGNOSTICS
    if(student_count_calls)++student_qscan_calls;
#endif
    double s = -w.qsum;
    for (size_t i = 0; i < v.codes.size(); ++i) {
        s += double(v.codes[i]) * w.scaled[i];
    }
    return s;
}

struct ProbeSearchStats {
    double reachable_objects = 0.0;
    double exact_evaluated = 0.0;
};

struct ProbeResult {
    vector<int> ids;
    ProbeSearchStats stats;
};

struct RoadQScanIndex {
    unique_ptr<QuantizedStore> poi;
    unique_ptr<QuantizedStore> cat;
    double build_ms = 0.0;
    double index_mb = 0.0;
};

static RoadQScanIndex buildRoadQScanIndex(Dataset& ds, int bits) {
    auto t0 = chrono::high_resolution_clock::now();
    RoadQScanIndex idx;
    idx.poi = make_unique<QuantizedStore>(*ds.poi_reader, bits);
    idx.cat = make_unique<QuantizedStore>(*ds.cat_reader, bits);
    idx.poi->buildAll();
    idx.cat->buildAll();
    auto t1 = chrono::high_resolution_clock::now();
    idx.build_ms = chrono::duration<double, milli>(t1 - t0).count();
    idx.index_mb = idx.poi->indexMB() + idx.cat->indexMB();
    return idx;
}

static double objectQuantizedScore(
    const ObjectRecord& obj,
    RoadQScanIndex& idx,
    const QueryWeights& poi_w,
    const QueryWeights& cat_w,
    bool upper_bound
) {
    const QuantizedVector& pv = idx.poi->get(obj.poi_id);
    double best = quantizedEstimate(pv, poi_w) + (upper_bound ? double(pv.error) : 0.0);
    for (int cid : obj.category_ids) {
        const QuantizedVector& cv = idx.cat->get(cid);
        double s = quantizedEstimate(cv, cat_w) + (upper_bound ? double(cv.error) : 0.0);
        if (s > best) best = s;
    }
    return best;
}

static ProbeResult roadRangeQScanEstimateOnly(
    Dataset& ds,
    RoadQScanIndex& idx,
    const vector<float>& q,
    int source_vertex,
    double tau,
    int k
) {
    ProbeResult out;
    QueryWeights poi_w = makeWeights(q, idx.poi->delta);
    QueryWeights cat_w = makeWeights(q, idx.cat->delta);
    priority_queue<Candidate> heap;
    vector<int> vertices = boundedDijkstraVertices(source_vertex - 1, tau, ds.road_n);
    for (int v : vertices) {
        for (int oi : ds.objects_by_vertex[v]) {
            const auto& obj = ds.objects[oi];
            out.stats.reachable_objects += 1.0;
            double score = objectQuantizedScore(obj, idx, poi_w, cat_w, false);
            pushTop(heap, k, {score, obj.object_id, obj.vertex_id});
        }
    }
    out.ids = heapToObjectIds(heap);
    return out;
}

static ProbeResult roadRangeQScanUbRerank(
    Dataset& ds,
    RoadQScanIndex& idx,
    const vector<float>& q,
    int source_vertex,
    double tau,
    int k
) {
    ProbeResult out;
    QueryWeights poi_w = makeWeights(q, idx.poi->delta);
    QueryWeights cat_w = makeWeights(q, idx.cat->delta);
    vector<pair<double, int>> ub_objects;
    vector<int> vertices = boundedDijkstraVertices(source_vertex - 1, tau, ds.road_n);
    for (int v : vertices) {
        for (int oi : ds.objects_by_vertex[v]) {
            const auto& obj = ds.objects[oi];
            out.stats.reachable_objects += 1.0;
            double ub = objectQuantizedScore(obj, idx, poi_w, cat_w, true);
            ub_objects.push_back({ub, obj.object_id});
        }
    }
    sort(ub_objects.begin(), ub_objects.end(), [](const auto& a, const auto& b) {
        if (fabs(a.first - b.first) > 1e-12) return a.first > b.first;
        return a.second < b.second;
    });
    priority_queue<Candidate> heap;
    for (const auto& row : ub_objects) {
        if (static_cast<int>(heap.size()) >= k && row.first <= heap.top().score + 1e-12) break;
        const auto& obj = ds.objects[row.second];
        pushTop(heap, k, {objectScore(q, obj, ds), obj.object_id, obj.vertex_id});
        out.stats.exact_evaluated += 1.0;
    }
    out.ids = heapToObjectIds(heap);
    return out;
}

template <class SearchFn>
static void runProbeQueries(
    const Options& opt,
    Dataset& ds,
    const string& algo,
    SearchFn search_fn,
    double build_ms,
    double index_mb,
    const string& status_extra
) {
    vector<double> times, reachable, exact_eval;
    double recall_score_sum = 0.0;
    double recall_id_sum = 0.0;
    for (size_t qi = 0; qi < ds.query_vecs.size(); ++qi) {
        auto t0 = chrono::high_resolution_clock::now();
        ProbeResult pred = search_fn(ds.query_vecs[qi], ds.query_meta[qi].source_vertex_id);
        auto t1 = chrono::high_resolution_clock::now();
        times.push_back(chrono::duration<double, milli>(t1 - t0).count());
        reachable.push_back(pred.stats.reachable_objects);
        exact_eval.push_back(pred.stats.exact_evaluated);
        recall_score_sum += recallAtScoreThreshold(pred.ids, ds.query_vecs[qi], ds, ds.query_meta[qi].query_id, opt.k);
        recall_id_sum += recallAtIds(pred.ids, ds, ds.query_meta[qi].query_id, opt.k);
    }
    double avg = accumulate(times.begin(), times.end(), 0.0) / double(times.size());
    double avg_reachable = accumulate(reachable.begin(), reachable.end(), 0.0) / double(reachable.size());
    double avg_exact = accumulate(exact_eval.begin(), exact_eval.end(), 0.0) / double(exact_eval.size());
    ofstream out(opt.results_csv, ios::app);
    out << fixed << setprecision(8)
        << "exp," << opt.region << "," << algo
        << "," << opt.param_name << "," << opt.param_value
        << "," << opt.query_tag << "," << opt.k << "," << int(opt.tau)
        << "," << opt.quant_bits << "," << opt.ef_search
        << "," << build_ms << "," << index_mb
        << "," << avg << "," << percentile(times, 0.5) << "," << percentile(times, 0.95)
        << "," << (avg > 0 ? 1000.0 / avg : 0.0)
        << "," << recall_score_sum / double(times.size())
        << "," << recall_id_sum / double(times.size())
        << ",0,0,ok;" << status_extra
        << ";avg_reachable_objects=" << avg_reachable
        << ";avg_exact_evaluated=" << avg_exact
        << ";p95_exact_evaluated=" << percentile(exact_eval, 0.95)
        << "\n";
}

static void writeHeaderIfNeeded(const string& path) {
    ifstream in(path);
    if (in.good() && in.peek() != ifstream::traits_type::eof()) return;
    ofstream out(path, ios::app);
    out << "experiment_id,dataset,algorithm,param_name,param_value,query_tag,k,tau,quant_bits,ef_search,"
        << "build_time_ms,index_size_mb,avg_query_ms,p50_ms,p95_ms,qps,recall_score,recall_id,"
        << "indexed_block_groups,indexed_aspects,status\n";
}

static string roadQScanMode() {
    const char* env = getenv("ROAD_QSCAN_MODE");
    string mode = env ? string(env) : "all";
    for (char& c : mode) c = char(tolower(static_cast<unsigned char>(c)));
    if (mode == "estimateonly") mode = "estimate_only";
    if (mode == "ubrerank") mode = "ub_rerank";
    if (mode != "all" && mode != "exact" && mode != "ub_rerank" && mode != "estimate_only") {
        throw runtime_error("Invalid ROAD_QSCAN_MODE=" + mode + " (expected all, exact, ub_rerank, estimate_only)");
    }
    return mode;
}

static bool shouldRunMode(const string& selected, const string& candidate) {
    return selected == "all" || selected == candidate;
}

int main(int argc, char** argv) {
    string mode = roadQScanMode();
    if (argc > 1 && string(argv[1]) == "--tasks") {
        string task_file;
        Options base = parseBatchArgs(argc, argv, task_file);
        writeHeaderIfNeeded(base.results_csv);
        vector<ExperimentTask> tasks = readTaskFile(task_file);
        if (tasks.empty()) throw runtime_error("Empty task file: " + task_file);
        Options first = optionsForTask(base, tasks[0]);
        Dataset ds = loadDataset(first);
        RoadQScanIndex qidx = buildRoadQScanIndex(ds, base.quant_bits);
        cout << "[Probe] ROAD_QSCAN_MODE=" << mode << endl;
        size_t task_i = 0;
        for (const auto& task : tasks) {
            Options opt = optionsForTask(base, task);
            loadTaskData(ds, opt);
            cout << "[Probe] " << opt.region << " task " << (++task_i) << "/" << tasks.size()
                 << " tau=" << opt.tau << " k=" << opt.k << endl;
            if (shouldRunMode(mode, "exact")) {
                runProbeQueries(opt, ds, "RoadRange+ExactScan-Probe", [&](const vector<float>& q, int s) {
                    ProbeResult out;
                    out.ids = roadRangeExactScan(ds, q, s, opt.tau, opt.k);
                    return out;
                }, 0.0, 0.0, "source=roadRangeExactScan");
            }
            if (shouldRunMode(mode, "ub_rerank")) {
                runProbeQueries(opt, ds, "RoadRange+QScan-UBRerank", [&](const vector<float>& q, int s) {
                    return roadRangeQScanUbRerank(ds, qidx, q, s, opt.tau, opt.k);
                }, qidx.build_ms, qidx.index_mb, "quantized_prebuilt=1;mode=ub_rerank");
            }
            if (shouldRunMode(mode, "estimate_only")) {
                runProbeQueries(opt, ds, "RoadRange+QScan-EstimateOnly", [&](const vector<float>& q, int s) {
                    return roadRangeQScanEstimateOnly(ds, qidx, q, s, opt.tau, opt.k);
                }, qidx.build_ms, qidx.index_mb, "quantized_prebuilt=1;mode=estimate_only");
            }
        }
        delete ds.poi_reader;
        delete ds.cat_reader;
        return 0;
    }
    Options opt = parseArgs(argc, argv);
    opt.results_csv = opt.results_csv.empty() ? "road_qscan_probe.csv" : opt.results_csv;
    writeHeaderIfNeeded(opt.results_csv);
    Dataset ds = loadDataset(opt);
    RoadQScanIndex qidx = buildRoadQScanIndex(ds, opt.quant_bits);
    cout << "[Probe] ROAD_QSCAN_MODE=" << mode << endl;
    if (shouldRunMode(mode, "exact")) {
        runProbeQueries(opt, ds, "RoadRange+ExactScan-Probe", [&](const vector<float>& q, int s) {
            ProbeResult out;
            out.ids = roadRangeExactScan(ds, q, s, opt.tau, opt.k);
            return out;
        }, 0.0, 0.0, "source=roadRangeExactScan");
    }
    if (shouldRunMode(mode, "ub_rerank")) {
        runProbeQueries(opt, ds, "RoadRange+QScan-UBRerank", [&](const vector<float>& q, int s) {
            return roadRangeQScanUbRerank(ds, qidx, q, s, opt.tau, opt.k);
        }, qidx.build_ms, qidx.index_mb, "quantized_prebuilt=1;mode=ub_rerank");
    }
    if (shouldRunMode(mode, "estimate_only")) {
        runProbeQueries(opt, ds, "RoadRange+QScan-EstimateOnly", [&](const vector<float>& q, int s) {
            return roadRangeQScanEstimateOnly(ds, qidx, q, s, opt.tau, opt.k);
        }, qidx.build_ms, qidx.index_mb, "quantized_prebuilt=1;mode=estimate_only");
    }
    delete ds.poi_reader;
    delete ds.cat_reader;
    return 0;
}
