#include "../../code/h2h.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <queue>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "rabitqlib/defines.hpp"
#include "rabitqlib/index/hnsw/hnsw.hpp"
#include "rabitqlib/index/ivf/ivf.hpp"
#include "rabitqlib/index/symqg/qg.hpp"
#include "rabitqlib/index/symqg/qg_builder.hpp"
#include "task_tsv_utils.hpp"

using namespace std;

typedef pair<int, double> ID;
vector<ID> G[20000000];

static string marsRoot() {
    const char* env = getenv("MARS_ROOT");
    if (env && env[0]) return string(env);
    ifstream local_data("data/NYC/USA-road-d.NYC.gr");
    if (local_data.good()) return string(".");
    ifstream parent_data("../data/NYC/USA-road-d.NYC.gr");
    if (parent_data.good()) return string("..");
    return string("..");
}

static string pathJoin(const string& a, const string& b) {
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

static string dataPath(const string& region, const string& leaf = "") {
    string p = pathJoin(pathJoin(marsRoot(), "data"), region);
    return leaf.empty() ? p : pathJoin(p, leaf);
}

struct QueryMeta {
    int query_id = 0;
    int source_vertex_id = 0;  // 1-based
    string query_text;
};

struct ObjectRecord {
    int object_id = 0;
    int vertex_id = 0;  // 1-based
    int poi_id = 0;     // 1-based
    vector<int> category_ids;
};

struct AspectEntry {
    int object_id = 0;
    int vertex_id = 0;  // 1-based
    int group_id = 0;   // 0..10
    int category_id = 0;
    bool is_category = false;
};

struct Candidate {
    double score = -1e100;
    int object_id = -1;
    int vertex_id = -1;
    bool operator<(const Candidate& other) const {
        if (fabs(score - other.score) > 1e-12) return score > other.score;
        return object_id < other.object_id;
    }
};

struct Options {
    string region;
    string algorithm;
    string query_tag;
    double tau = 5000.0;
    int k = 10;
    string gt_path;
    string results_csv;
    string param_name = "default";
    string param_value = "default";
    int quant_bits = 5;
    int ef_search = 100;
    int candidate_budget = 1000;
    int max_candidate_budget = 0;  // 0 means up to all indexed aspect vectors
    int candidate_growth = 2;
    int hnsw_m = 16;
    int hnsw_ef_build = 100;
    uint64_t ann_seed = 100;
    int qg_degree = 32;
    int qg_ef_build = 200;
    int ivf_nprobe = 8;
    int mars_threshold = 5500;
    double mars_graph_selectivity = 0.03;
    int mars_graph_budget = 0;  // 0 means use ef_search for MARS graph path
    string mars_graph_mode = "global";  // global or local
    bool mars_graph_first_fallback = false;
    bool mars_graph_refill_scan = false;
    double mars_graph_refine_selectivity = 0.0;  // 0 disables exact refinement after graph search
    int mars_local_graph_level = 3;
    size_t mars_local_graph_min_aspects = 20000;
    int mesh_leaf_capacity = 10000;
    double mesh_memory_mb = 8000.0;
    int mesh_ef = 500;
    int mesh_candidate_budget = 500;
    int mesh_max_candidates = 500;
    uint64_t mesh_seed = 100;
    string mesh_training_tag = "q1500_seed20260807_natural_medium";
    string mesh_placement_taus = "2000,4000,6000,8000,10000";
    string mesh_frozen_placement_manifest;
    string mesh_index_dir;
    size_t max_index_vectors = 0;  // 0 means full data
    bool build_ablation_indexes = false;
};

struct ExperimentTask {
    string query_tag;
    double tau = 5000.0;
    int k = 10;
    string gt_path;
    string param_name;
    string param_value;
};

class FvecsReader {
   public:
    explicit FvecsReader(const string& path) : path_(path), in_(path, ios::binary) {
        if (!in_.is_open()) throw runtime_error("Cannot open fvecs: " + path);
        in_.read(reinterpret_cast<char*>(&dim_), sizeof(int));
        if (!in_ || dim_ <= 0) throw runtime_error("Bad fvecs dim: " + path);
        row_bytes_ = sizeof(int) + sizeof(float) * dim_;
        in_.seekg(0, ios::end);
        rows_ = static_cast<long long>(in_.tellg()) / row_bytes_;
    }
    int dim() const { return dim_; }
    long long rows() const { return rows_; }
    vector<float> readNorm1Based(int id) {
        if (id <= 0 || id > rows_) throw runtime_error(path_ + ": id out of range");
        auto it = cache_.find(id);
        if (it != cache_.end()) return it->second;
        in_.seekg(static_cast<long long>(id - 1) * row_bytes_, ios::beg);
        int row_dim = 0;
        in_.read(reinterpret_cast<char*>(&row_dim), sizeof(int));
        if (!in_ || row_dim != dim_) throw runtime_error(path_ + ": bad row dim");
        vector<float> v(dim_);
        in_.read(reinterpret_cast<char*>(v.data()), sizeof(float) * dim_);
        normalize(v);
        if (cache_.size() < max_cache_) cache_.emplace(id, v);
        return v;
    }
   private:
    static void normalize(vector<float>& v) {
        double s = 0.0;
        for (float x : v) s += double(x) * double(x);
        double n = sqrt(s);
        if (n > 0) for (float& x : v) x = float(double(x) / n);
    }
    string path_;
    ifstream in_;
    int dim_ = 0;
    long long row_bytes_ = 0;
    long long rows_ = 0;
    size_t max_cache_ = 200000;
    unordered_map<int, vector<float>> cache_;
};

static vector<float> normalize(vector<float> v) {
    double s = 0.0;
    for (float x : v) s += double(x) * double(x);
    double n = sqrt(s);
    if (n > 0) for (float& x : v) x = float(double(x) / n);
    return v;
}

static double dotProduct(const vector<float>& a, const vector<float>& b) {
    double s = 0.0;
    for (size_t i = 0; i < a.size(); ++i) s += double(a[i]) * double(b[i]);
    return s;
}

static vector<vector<float>> readFvecs(const string& path) {
    ifstream in(path, ios::binary);
    if (!in.is_open()) throw runtime_error("Cannot open fvecs: " + path);
    vector<vector<float>> rows;
    while (true) {
        int dim = 0;
        in.read(reinterpret_cast<char*>(&dim), sizeof(int));
        if (!in) break;
        vector<float> v(dim);
        in.read(reinterpret_cast<char*>(v.data()), sizeof(float) * dim);
        rows.push_back(normalize(v));
    }
    return rows;
}

static vector<QueryMeta> readQueryMeta(const string& path) {
    ifstream in(path);
    if (!in.is_open()) throw runtime_error("Cannot open query meta: " + path);
    vector<QueryMeta> rows;
    string line;
    getline(in, line);
    while (getline(in, line)) {
        stringstream ss(line);
        string qid, vertex, text, group, seed;
        getline(ss, qid, '\t');
        getline(ss, vertex, '\t');
        getline(ss, text, '\t');
        getline(ss, group, '\t');
        getline(ss, seed, '\t');
        if (!qid.empty() && !vertex.empty()) rows.push_back({stoi(qid), stoi(vertex), text});
    }
    return rows;
}

static vector<ObjectRecord> parseObjects(const string& path) {
    ifstream in(path);
    if (!in.is_open()) throw runtime_error("Cannot open mapping: " + path);
    vector<ObjectRecord> objects;
    regex object_re("^(\\d+)<(.*)$");
    string line;
    int vertex = -1, oid = 0;
    while (getline(in, line)) {
        if (line.empty()) continue;
        if (line.back() == '-') {
            vertex = stoi(line.substr(0, line.size() - 1));
            continue;
        }
        smatch m;
        if (regex_match(line, m, object_re)) {
            ObjectRecord obj;
            obj.object_id = oid++;
            obj.vertex_id = vertex;
            obj.poi_id = stoi(m[1]);
            stringstream ss(m[2]);
            int cid;
            while (ss >> cid) obj.category_ids.push_back(cid);
            objects.push_back(obj);
        }
    }
    return objects;
}

static vector<vector<int>> buildObjectsByVertex(const vector<ObjectRecord>& objects, int road_n) {
    vector<vector<int>> by_vertex(road_n + 1);
    for (size_t i = 0; i < objects.size(); ++i) {
        int v = objects[i].vertex_id;
        if (v >= 1 && v <= road_n) by_vertex[v].push_back(int(i));
    }
    return by_vertex;
}

static void loadRoadGraph(const string& region, int& road_n, int& road_m) {
    string graphfile = dataPath(region, "USA-road-d." + region + ".gr");
    FILE* fp = fopen(graphfile.c_str(), "r");
    if (!fp) throw runtime_error("Cannot open graph: " + graphfile);
    char ch, buffer[100];
    int u, v, intw;
    double w;
    for (int i = 0; i < 4; i++) fgets(buffer, 90, fp);
    for (int i = 0; i < 4; i++) fgetc(fp);
    fscanf(fp, "%d%d", &road_n, &road_m);
    for (int i = 0; i < 3; i++) fgets(buffer, 90, fp);
    for (int i = 0; i < road_m; i++) {
        fscanf(fp, "%c%d%d%d", &ch, &u, &v, &intw);
        fgets(buffer, 90, fp);
        w = double(intw) / 10.0;
        u--; v--;
        if (i % 2 == 0) {
            G[u].push_back(ID(v, w));
            G[v].push_back(ID(u, w));
        }
    }
    fclose(fp);
}

static vector<int> boundedDijkstraVertices(int source0, double tau, int road_n) {
    vector<double> dist(road_n, DBL_MAX);
    vector<int> touched;
    priority_queue<pair<double,int>, vector<pair<double,int>>, greater<pair<double,int>>> pq;
    dist[source0] = 0.0;
    pq.push({0.0, source0});
    while (!pq.empty()) {
        auto cur = pq.top(); pq.pop();
        double d = cur.first;
        int u = cur.second;
        if (d != dist[u] || d > tau) continue;
        touched.push_back(u + 1);
        for (const ID& e : G[u]) {
            int v = e.first;
            double nd = d + e.second;
            if (nd <= tau && nd < dist[v]) {
                dist[v] = nd;
                pq.push({nd, v});
            }
        }
    }
    return touched;
}

struct GroundTruthData {
    unordered_map<int, vector<int>> ids;
    unordered_map<int, double> score_threshold;
    unordered_map<int, int> result_count;
};

static GroundTruthData loadGroundTruth(const string& path, int k) {
    ifstream in(path);
    if (!in.is_open()) throw runtime_error("Cannot open ground truth: " + path);
    GroundTruthData gt;
    string line;
    getline(in, line);
    while (getline(in, line)) {
        stringstream ss(line);
        string qid, rank, object_id, vertex_id, poi_id, score;
        getline(ss, qid, '\t');
        getline(ss, rank, '\t');
        getline(ss, object_id, '\t');
        getline(ss, vertex_id, '\t');
        getline(ss, poi_id, '\t');
        getline(ss, score, '\t');
        if (!qid.empty() && !object_id.empty()) {
            int q = stoi(qid);
            gt.ids[q].push_back(stoi(object_id));
            gt.result_count[q]++;
            if (!score.empty() && gt.result_count[q] <= k) {
                gt.score_threshold[q] = atof(score.c_str());
            }
        }
    }
    return gt;
}

struct CategoryMap {
    unordered_map<int,int> cat_to_group;
    vector<int> group_cat_id;
    vector<vector<float>> group_centers;
};

static vector<string> splitCsvLine(const string& line) {
    vector<string> out;
    string cur;
    bool quote = false;
    for (char c : line) {
        if (c == '"') quote = !quote;
        else if (c == ',' && !quote) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

static CategoryMap loadCategoryMap(const string& region, FvecsReader& cat_reader) {
    static const vector<string> level1_names = {
        "Arts and Entertainment",
        "Business and Professional Services",
        "Community and Government",
        "Dining and Drinking",
        "Event",
        "Health and Medicine",
        "Landmarks and Outdoors",
        "Nightlife Spot",
        "Retail",
        "Sports and Recreation",
        "Travel and Transportation",
    };
    string path = dataPath(region, "categories_sorted.csv");
    ifstream in(path);
    if (!in.is_open()) throw runtime_error("Cannot open categories_sorted.csv: " + path);
    string header, line;
    getline(in, header);
    vector<vector<string>> rows;
    unordered_map<string,int> name_to_row;
    int row_id = 1;
    while (getline(in, line)) {
        auto cols = splitCsvLine(line);
        rows.push_back(cols);
        if (cols.size() > 2 && !cols[2].empty() && !name_to_row.count(cols[2])) name_to_row[cols[2]] = row_id;
        row_id++;
    }
    CategoryMap cm;
    cm.group_cat_id.resize(level1_names.size());
    cm.group_centers.resize(level1_names.size());
    unordered_map<int,int> l1cat_to_group;
    for (size_t g = 0; g < level1_names.size(); ++g) {
        int cat_id = name_to_row[level1_names[g]];
        cm.group_cat_id[g] = cat_id;
        cm.group_centers[g] = cat_reader.readNorm1Based(cat_id);
        l1cat_to_group[cat_id] = int(g);
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        string l1 = rows[i].size() > 5 ? rows[i][5] : rows[i][2];
        int l1_cat = name_to_row[l1];
        cm.cat_to_group[int(i) + 1] = l1cat_to_group[l1_cat];
    }
    return cm;
}

static bool betterCandidate(const Candidate& a, const Candidate& b) {
    if (fabs(a.score - b.score) > 1e-12) return a.score > b.score;
    return a.object_id < b.object_id;
}

static void pushTop(priority_queue<Candidate>& heap, int k, Candidate cand) {
    if ((int)heap.size() < k) heap.push(cand);
    else if (betterCandidate(cand, heap.top())) { heap.pop(); heap.push(cand); }
}

static vector<int> heapToObjectIds(priority_queue<Candidate> heap) {
    vector<Candidate> rows;
    while (!heap.empty()) { rows.push_back(heap.top()); heap.pop(); }
    sort(rows.begin(), rows.end(), [](const Candidate& a, const Candidate& b){ return betterCandidate(a, b); });
    vector<int> ids;
    for (auto& r : rows) ids.push_back(r.object_id);
    return ids;
}

static double percentile(vector<double> v, double p) {
    if (v.empty()) return 0;
    sort(v.begin(), v.end());
    size_t idx = min(v.size() - 1, size_t(max(1.0, ceil(p * v.size())) - 1));
    return v[idx];
}

struct Dataset {
    string region, prefix;
    int road_n = 0, road_m = 0;
    vector<ObjectRecord> objects;
    vector<vector<int>> objects_by_vertex;
    vector<QueryMeta> query_meta;
    vector<vector<float>> query_vecs;
    GroundTruthData gt;
    FvecsReader *poi_reader = nullptr;
    FvecsReader *cat_reader = nullptr;
    CategoryMap category_map;
};

static double objectScore(const vector<float>& q, const ObjectRecord& obj, Dataset& ds) {
    double best = dotProduct(q, ds.poi_reader->readNorm1Based(obj.poi_id));
    for (int cid : obj.category_ids) best = max(best, dotProduct(q, ds.cat_reader->readNorm1Based(cid)));
    return best;
}

static double recallAtScoreThreshold(
    const vector<int>& pred,
    const vector<float>& q,
    Dataset& ds,
    int query_id,
    int k
) {
    auto count_it = ds.gt.result_count.find(query_id);
    int denom = count_it == ds.gt.result_count.end() ? 0 : min(k, count_it->second);
    if (denom <= 0) return 1.0;

    auto threshold_it = ds.gt.score_threshold.find(query_id);
    if (threshold_it == ds.gt.score_threshold.end()) return 0.0;
    double threshold = threshold_it->second;

    unordered_set<int> seen;
    int accepted = 0;
    constexpr double EPS = 1e-6;
    for (int object_id : pred) {
        if (object_id < 0 || object_id >= static_cast<int>(ds.objects.size())) continue;
        if (!seen.insert(object_id).second) continue;
        double score = objectScore(q, ds.objects[object_id], ds);
        if (score + EPS >= threshold) accepted++;
    }
    return double(min(accepted, denom)) / double(denom);
}

static double recallAtIds(const vector<int>& pred, Dataset& ds, int query_id, int k) {
    auto gt_it = ds.gt.ids.find(query_id);
    if (gt_it == ds.gt.ids.end()) return 0.0;
    int denom = min(k, static_cast<int>(gt_it->second.size()));
    if (denom <= 0) return 1.0;

    unordered_set<int> gt_ids;
    for (int object_id : gt_it->second) {
        if (static_cast<int>(gt_ids.size()) >= denom) break;
        gt_ids.insert(object_id);
    }

    unordered_set<int> seen;
    int accepted = 0;
    for (int object_id : pred) {
        if (!seen.insert(object_id).second) continue;
        if (gt_ids.count(object_id)) accepted++;
    }
    return double(min(accepted, denom)) / double(denom);
}

static int objectGroup(const ObjectRecord& obj, const CategoryMap& cm) {
    if (obj.category_ids.empty()) return 0;
    auto it = cm.cat_to_group.find(obj.category_ids[0]);
    return it == cm.cat_to_group.end() ? 0 : it->second;
}

static Dataset loadDataset(const Options& opt) {
    Dataset ds;
    ds.region = opt.region;
    ds.prefix = dataPath(opt.region) + "/";
    loadRoadGraph(opt.region, ds.road_n, ds.road_m);
    ds.objects = parseObjects(ds.prefix + "poi_node_mapping_Ext3.txt");
    ds.objects_by_vertex = buildObjectsByVertex(ds.objects, ds.road_n);
    ds.poi_reader = new FvecsReader(ds.prefix + "poi.fvecs");
    ds.cat_reader = new FvecsReader(ds.prefix + "cat.fvecs");
    ds.category_map = loadCategoryMap(opt.region, *ds.cat_reader);
    ds.query_meta = readQueryMeta(ds.prefix + "queries/" + opt.query_tag + ".meta.tsv");
    ds.query_vecs = readFvecs(ds.prefix + "queries/" + opt.query_tag + ".fvecs");
    ds.gt = loadGroundTruth(opt.gt_path, opt.k);
    return ds;
}

static void loadTaskData(Dataset& ds, const Options& opt) {
    ds.query_meta = readQueryMeta(ds.prefix + "queries/" + opt.query_tag + ".meta.tsv");
    ds.query_vecs = readFvecs(ds.prefix + "queries/" + opt.query_tag + ".fvecs");
    ds.gt = loadGroundTruth(opt.gt_path, opt.k);
}

static vector<int> roadRangeExactScan(Dataset& ds, const vector<float>& q, int source_vertex, double tau, int k) {
    priority_queue<Candidate> heap;
    vector<int> vertices = boundedDijkstraVertices(source_vertex - 1, tau, ds.road_n);
    for (int v : vertices) {
        for (int oi : ds.objects_by_vertex[v]) {
            const auto& obj = ds.objects[oi];
            pushTop(heap, k, {objectScore(q, obj, ds), obj.object_id, obj.vertex_id});
        }
    }
    return heapToObjectIds(heap);
}

static vector<int> marsLite(Dataset& ds, const vector<float>& q, int source_vertex, double tau, int k, int threshold) {
    priority_queue<Candidate> heap;
    vector<vector<int>> group_objects(11);
    vector<int> vertices = boundedDijkstraVertices(source_vertex - 1, tau, ds.road_n);
    for (int v : vertices) {
        for (int oi : ds.objects_by_vertex[v]) {
            int g = objectGroup(ds.objects[oi], ds.category_map);
            group_objects[g].push_back(oi);
        }
    }
    double theta = -1e100;
    for (int g = 0; g < 11; ++g) {
        if (group_objects[g].empty()) continue;
        // Use a radius-2 ball for the unit-vector score bound.
        double ub = dotProduct(q, ds.category_map.group_centers[g]) + 2.0;
        if ((int)heap.size() >= k) theta = heap.top().score;
        if (ub <= theta) continue;
        for (int oi : group_objects[g]) {
            const auto& obj = ds.objects[oi];
            pushTop(heap, k, {objectScore(q, obj, ds), obj.object_id, obj.vertex_id});
        }
    }
    return heapToObjectIds(heap);
}

struct GlobalIndexData {
    vector<float> matrix;
    vector<AspectEntry> entries;
    vector<vector<int>> entry_objects;
    vector<rabitqlib::PID> cluster_ids;
    vector<float> centroids;
    vector<size_t> poi_aspect_by_object;
    unordered_map<int, size_t> cat_aspect_by_id;
    size_t dim = 0;
};

static GlobalIndexData buildGlobalIndexData(Dataset& ds, size_t max_vectors) {
    GlobalIndexData gd;
    gd.dim = ds.query_vecs.empty() ? 0 : ds.query_vecs[0].size();
    gd.centroids.resize(11 * gd.dim);
    for (int g = 0; g < 11; ++g) {
        copy(ds.category_map.group_centers[g].begin(), ds.category_map.group_centers[g].end(), gd.centroids.begin() + g * gd.dim);
    }
    gd.poi_aspect_by_object.assign(ds.objects.size(), numeric_limits<size_t>::max());
    unordered_map<int, vector<int>> cat_objects;
    for (const auto& obj : ds.objects) {
        if (max_vectors && gd.entries.size() >= max_vectors) break;
        int g = objectGroup(obj, ds.category_map);
        auto pv = ds.poi_reader->readNorm1Based(obj.poi_id);
        size_t aspect_id = gd.entries.size();
        gd.matrix.insert(gd.matrix.end(), pv.begin(), pv.end());
        gd.entries.push_back({obj.object_id, obj.vertex_id, g, 0, false});
        gd.entry_objects.push_back({obj.object_id});
        gd.cluster_ids.push_back(g);
        if (obj.object_id >= 0 && obj.object_id < static_cast<int>(gd.poi_aspect_by_object.size())) {
            gd.poi_aspect_by_object[obj.object_id] = aspect_id;
        }
        for (int cid : obj.category_ids) {
            cat_objects[cid].push_back(obj.object_id);
        }
    }
    vector<int> cat_ids;
    cat_ids.reserve(cat_objects.size());
    for (const auto& row : cat_objects) cat_ids.push_back(row.first);
    sort(cat_ids.begin(), cat_ids.end());
    for (int cid : cat_ids) {
        if (max_vectors && gd.entries.size() >= max_vectors) break;
        auto objects = cat_objects[cid];
        sort(objects.begin(), objects.end());
        objects.erase(unique(objects.begin(), objects.end()), objects.end());
        if (objects.empty()) continue;
        int fallback_group = objectGroup(ds.objects[objects.front()], ds.category_map);
        int cg = ds.category_map.cat_to_group.count(cid) ? ds.category_map.cat_to_group[cid] : fallback_group;
        auto cv = ds.cat_reader->readNorm1Based(cid);
        size_t aspect_id = gd.entries.size();
        gd.matrix.insert(gd.matrix.end(), cv.begin(), cv.end());
        gd.entries.push_back({-1, -1, cg, cid, true});
        gd.entry_objects.push_back(std::move(objects));
        gd.cluster_ids.push_back(cg);
        gd.cat_aspect_by_id[cid] = aspect_id;
    }
    return gd;
}

struct GlobalAggregateResult {
    vector<int> ids;
    int unique_objects = 0;
    int road_verified_objects = 0;
};

struct H2HAspectFeasibilityCache {
    Dataset& ds;
    const GlobalIndexData& gd;
    int source_vertex = 1;
    double tau = 0.0;
    vector<int8_t> memo;
    vector<int8_t> object_memo;
    int checked_aspects = 0;
    int checked_objects = 0;

    H2HAspectFeasibilityCache(Dataset& ds_, const GlobalIndexData& gd_, int source_vertex_, double tau_)
        : ds(ds_), gd(gd_), source_vertex(source_vertex_), tau(tau_), memo(gd_.entries.size(), -1),
          object_memo(ds_.objects.size(), -1) {}

    bool objectFeasible(int object_id) {
        if (object_id < 0 || object_id >= static_cast<int>(ds.objects.size())) return false;
        int8_t& cached = object_memo[object_id];
        if (cached >= 0) return cached != 0;
        checked_objects++;
        const auto& obj = ds.objects[object_id];
        double rd = h2hQuery(source_vertex - 1, obj.vertex_id - 1);
        cached = (rd >= 0 && rd <= tau) ? 1 : 0;
        return cached != 0;
    }

    bool operator()(rabitqlib::PID aspect_id) {
        if (aspect_id >= gd.entries.size()) return false;
        int8_t& cached = memo[aspect_id];
        if (cached >= 0) return cached != 0;
        checked_aspects++;
        bool ok = false;
        const vector<int>* objects = aspect_id < gd.entry_objects.size() ? &gd.entry_objects[aspect_id] : nullptr;
        if (objects) {
            for (int object_id : *objects) {
                if (objectFeasible(object_id)) {
                    ok = true;
                    break;
                }
            }
        }
        cached = ok ? 1 : 0;
        return ok;
    }
};

static GlobalAggregateResult aggregateGlobalCandidatesDetailed(
    Dataset& ds, const GlobalIndexData& gd, const vector<float>& q, const vector<rabitqlib::PID>& ids, int source_vertex, double tau, int k
) {
    priority_queue<Candidate> heap;
    unordered_set<int> seen;
    int road_verified = 0;
    for (auto vid : ids) {
        if (vid >= gd.entries.size()) continue;
        const vector<int>* objects = vid < gd.entry_objects.size() ? &gd.entry_objects[vid] : nullptr;
        if (!objects) continue;
        for (int object_id : *objects) {
            if (object_id < 0 || object_id >= static_cast<int>(ds.objects.size())) continue;
            if (!seen.insert(object_id).second) continue;
            const auto& obj = ds.objects[object_id];
            double rd = h2hQuery(source_vertex - 1, obj.vertex_id - 1);
            if (rd > tau) continue;
            road_verified++;
            pushTop(heap, k, {objectScore(q, obj, ds), obj.object_id, obj.vertex_id});
        }
    }
    return {heapToObjectIds(heap), static_cast<int>(seen.size()), road_verified};
}

static vector<int> aggregateGlobalCandidates(
    Dataset& ds, const GlobalIndexData& gd, const vector<float>& q, const vector<rabitqlib::PID>& ids, int source_vertex, double tau, int k
) {
    return aggregateGlobalCandidatesDetailed(ds, gd, q, ids, source_vertex, tau, k).ids;
}

template <class FeasibleFn>
static GlobalAggregateResult aggregateFilteredGlobalCandidatesDetailed(
    Dataset& ds,
    const GlobalIndexData& gd,
    const vector<float>& q,
    const vector<rabitqlib::PID>& ids,
    int k,
    FeasibleFn& feasible
) {
    priority_queue<Candidate> heap;
    unordered_set<int> seen;
    int road_verified = 0;
    for (auto vid : ids) {
        if (vid >= gd.entries.size()) continue;
        const vector<int>* objects = vid < gd.entry_objects.size() ? &gd.entry_objects[vid] : nullptr;
        if (!objects) continue;
        for (int object_id : *objects) {
            if (object_id < 0 || object_id >= static_cast<int>(ds.objects.size())) continue;
            if (!seen.insert(object_id).second) continue;
            if (!feasible.objectFeasible(object_id)) continue;
            road_verified++;
            const auto& obj = ds.objects[object_id];
            pushTop(heap, k, {objectScore(q, obj, ds), obj.object_id, obj.vertex_id});
        }
    }
    return {heapToObjectIds(heap), static_cast<int>(seen.size()), road_verified};
}

template <class SearchIdsFn>
static vector<int> adaptiveGlobalCandidates(
    Dataset& ds,
    const GlobalIndexData& gd,
    const vector<float>& q,
    int source_vertex,
    double tau,
    int k,
    int initial_budget,
    int max_budget,
    int growth,
    SearchIdsFn search_ids
) {
    int full_limit = static_cast<int>(min<size_t>(gd.entries.size(), static_cast<size_t>(INT_MAX)));
    int budget = min(full_limit, max(k, initial_budget));
    int limit = max_budget > 0 ? min(full_limit, max(budget, max_budget)) : full_limit;
    growth = max(2, growth);
    vector<int> best;
    while (true) {
        vector<rabitqlib::PID> ids = search_ids(budget);
        best = aggregateGlobalCandidates(ds, gd, q, ids, source_vertex, tau, k);
        if ((int)best.size() >= k || budget >= limit) return best;
        long long next_budget = 1LL * budget * growth;
        budget = int(min<long long>(limit, next_budget));
    }
}

struct TimedSearchResult {
    vector<int> ids;
    double time_ms = 0.0;
    int attempts = 0;
    int final_budget = 0;
    int returned_size = 0;
    int road_verified_objects = 0;
    int unique_objects = 0;
};

template <class SearchIdsFn>
static TimedSearchResult adaptiveFilteredGlobalCandidatesTimedCumulative(
    Dataset& ds,
    const GlobalIndexData& gd,
    const vector<float>& q,
    int source_vertex,
    double tau,
    int k,
    int initial_budget,
    int max_budget,
    int growth,
    SearchIdsFn search_ids
) {
    int full_limit = static_cast<int>(min<size_t>(gd.entries.size(), static_cast<size_t>(INT_MAX)));
    int budget = min(full_limit, max(k, initial_budget));
    int limit = max_budget > 0 ? min(full_limit, max(budget, max_budget)) : full_limit;
    growth = max(2, growth);
    TimedSearchResult result;
    while (true) {
        H2HAspectFeasibilityCache feasible(ds, gd, source_vertex, tau);
        auto t0 = chrono::high_resolution_clock::now();
        vector<rabitqlib::PID> ids = search_ids(budget, feasible);
        GlobalAggregateResult agg = aggregateFilteredGlobalCandidatesDetailed(ds, gd, q, ids, k, feasible);
        auto t1 = chrono::high_resolution_clock::now();
        result.time_ms += chrono::duration<double, milli>(t1 - t0).count();
        result.ids = std::move(agg.ids);
        result.attempts++;
        result.final_budget = budget;
        result.returned_size = static_cast<int>(result.ids.size());
        result.unique_objects = agg.unique_objects;
        result.road_verified_objects = agg.road_verified_objects;
        if ((int)result.ids.size() >= k || budget >= limit) return result;
        long long next_budget = 1LL * budget * growth;
        budget = int(min<long long>(limit, next_budget));
    }
}

template <class SearchIdsFn>
static TimedSearchResult adaptiveGlobalCandidatesTimedCumulative(
    Dataset& ds,
    const GlobalIndexData& gd,
    const vector<float>& q,
    int source_vertex,
    double tau,
    int k,
    int initial_budget,
    int max_budget,
    int growth,
    SearchIdsFn search_ids
) {
    int full_limit = static_cast<int>(min<size_t>(gd.entries.size(), static_cast<size_t>(INT_MAX)));
    int budget = min(full_limit, max(k, initial_budget));
    int limit = max_budget > 0 ? min(full_limit, max(budget, max_budget)) : full_limit;
    growth = max(2, growth);
    TimedSearchResult result;
    while (true) {
        auto t0 = chrono::high_resolution_clock::now();
        vector<rabitqlib::PID> ids = search_ids(budget);
        GlobalAggregateResult agg = aggregateGlobalCandidatesDetailed(ds, gd, q, ids, source_vertex, tau, k);
        auto t1 = chrono::high_resolution_clock::now();
        result.time_ms += chrono::duration<double, milli>(t1 - t0).count();
        result.ids = std::move(agg.ids);
        result.attempts++;
        result.final_budget = budget;
        result.returned_size = static_cast<int>(result.ids.size());
        result.unique_objects = agg.unique_objects;
        result.road_verified_objects = agg.road_verified_objects;
        if ((int)result.ids.size() >= k || budget >= limit) return result;
        long long next_budget = 1LL * budget * growth;
        budget = int(min<long long>(limit, next_budget));
    }
}

template <class SearchFn>
static void runQueries(
    const Options& opt,
    Dataset& ds,
    const string& algo,
    SearchFn search_fn,
    double build_ms,
    double index_mb,
    size_t indexed_block_groups = 0,
    size_t indexed_aspects = 0,
    const string& status_extra = ""
) {
    vector<double> times;
    double recall_score_sum = 0.0;
    double recall_id_sum = 0.0;
    for (size_t qi = 0; qi < ds.query_vecs.size(); ++qi) {
        auto t0 = chrono::high_resolution_clock::now();
        vector<int> pred = search_fn(ds.query_vecs[qi], ds.query_meta[qi].source_vertex_id);
        auto t1 = chrono::high_resolution_clock::now();
        times.push_back(chrono::duration<double, milli>(t1 - t0).count());
        recall_score_sum += recallAtScoreThreshold(pred, ds.query_vecs[qi], ds, ds.query_meta[qi].query_id, opt.k);
        recall_id_sum += recallAtIds(pred, ds, ds.query_meta[qi].query_id, opt.k);
    }
    double avg = accumulate(times.begin(), times.end(), 0.0) / double(times.size());
    ofstream out(opt.results_csv, ios::app);
    string status = "ok";
    if (algo.rfind("Global", 0) == 0) {
        status = "adaptive_candidates_" + to_string(opt.candidate_budget) + "_to_" + to_string(opt.max_candidate_budget);
    }
    if (!status_extra.empty()) status += ";" + status_extra;
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
        << "," << indexed_block_groups << "," << indexed_aspects
        << "," << status << "\n";
}

template <class SearchFn>
static void runTimedQueries(
    const Options& opt, Dataset& ds, const string& algo, SearchFn search_fn, double build_ms, double index_mb
) {
    vector<double> times;
    vector<double> internal_times;
    vector<double> attempts;
    vector<double> final_budgets;
    vector<double> returned_sizes;
    vector<double> road_verified;
    vector<double> unique_objects;
    int underfilled_queries = 0;
    double recall_score_sum = 0.0;
    double recall_id_sum = 0.0;
    for (size_t qi = 0; qi < ds.query_vecs.size(); ++qi) {
        auto outer_begin = chrono::steady_clock::now();
        TimedSearchResult pred = search_fn(ds.query_vecs[qi], ds.query_meta[qi].source_vertex_id);
        auto outer_end = chrono::steady_clock::now();
        times.push_back(chrono::duration<double, milli>(outer_end - outer_begin).count());
        internal_times.push_back(pred.time_ms);
        attempts.push_back(pred.attempts);
        final_budgets.push_back(pred.final_budget);
        returned_sizes.push_back(pred.returned_size);
        if (pred.returned_size < opt.k) underfilled_queries++;
        road_verified.push_back(pred.road_verified_objects);
        unique_objects.push_back(pred.unique_objects);
        recall_score_sum += recallAtScoreThreshold(pred.ids, ds.query_vecs[qi], ds, ds.query_meta[qi].query_id, opt.k);
        recall_id_sum += recallAtIds(pred.ids, ds, ds.query_meta[qi].query_id, opt.k);
    }
    double avg = accumulate(times.begin(), times.end(), 0.0) / double(times.size());
    ofstream out(opt.results_csv, ios::app);
    string max_budget_label = opt.max_candidate_budget > 0 ? to_string(opt.max_candidate_budget) : "full";
    string status = "in_search_h2h_filter_adaptive_cumulative_" + to_string(opt.candidate_budget) + "_to_" + max_budget_label;
    status += ";avg_attempts=" + to_string(accumulate(attempts.begin(), attempts.end(), 0.0) / double(attempts.size()));
    status += ";avg_final_budget=" + to_string(accumulate(final_budgets.begin(), final_budgets.end(), 0.0) / double(final_budgets.size()));
    status += ";p95_final_budget=" + to_string(percentile(final_budgets, 0.95));
    status += ";avg_returned_size=" + to_string(accumulate(returned_sizes.begin(), returned_sizes.end(), 0.0) / double(returned_sizes.size()));
    status += ";underfilled_rate=" + to_string(double(underfilled_queries) / double(returned_sizes.size()));
    status += ";avg_road_verified_objects=" + to_string(accumulate(road_verified.begin(), road_verified.end(), 0.0) / double(road_verified.size()));
    status += ";avg_unique_objects=" + to_string(accumulate(unique_objects.begin(), unique_objects.end(), 0.0) / double(unique_objects.size()));
    status += ";avg_internal_search_ms=" + to_string(accumulate(internal_times.begin(), internal_times.end(), 0.0) / double(internal_times.size()));
    status += ";timing_boundary=outer_search_call";
    status += ";ann_seed=" + to_string(opt.ann_seed);
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
        << ",0,0," << status << "\n";
}

static bool applyOption(Options& o, const string& arg) {
    size_t pos = arg.find('=');
    if (pos == string::npos) return false;
    string key = arg.substr(0, pos);
    string val = arg.substr(pos + 1);
    if (key == "quant_bits") o.quant_bits = stoi(val);
    else if (key == "ef_search") o.ef_search = stoi(val);
    else if (key == "candidate_budget") o.candidate_budget = stoi(val);
    else if (key == "max_candidate_budget") o.max_candidate_budget = stoi(val);
    else if (key == "candidate_growth") o.candidate_growth = stoi(val);
    else if (key == "hnsw_m") o.hnsw_m = stoi(val);
    else if (key == "hnsw_ef_build") o.hnsw_ef_build = stoi(val);
    else if (key == "ann_seed") o.ann_seed = stoull(val);
    else if (key == "qg_degree") o.qg_degree = stoi(val);
    else if (key == "qg_ef_build") o.qg_ef_build = stoi(val);
    else if (key == "ivf_nprobe") o.ivf_nprobe = stoi(val);
    else if (key == "mars_threshold") o.mars_threshold = stoi(val);
    else if (key == "mars_graph_selectivity") o.mars_graph_selectivity = atof(val.c_str());
    else if (key == "mars_graph_budget") o.mars_graph_budget = stoi(val);
    else if (key == "mars_graph_mode") o.mars_graph_mode = val;
    else if (key == "mars_graph_first_fallback") o.mars_graph_first_fallback = (stoi(val) != 0);
    else if (key == "mars_graph_refill_scan") o.mars_graph_refill_scan = (stoi(val) != 0);
    else if (key == "mars_graph_refine_selectivity") o.mars_graph_refine_selectivity = atof(val.c_str());
    else if (key == "mars_local_graph_level") o.mars_local_graph_level = stoi(val);
    else if (key == "mars_local_graph_min_aspects") o.mars_local_graph_min_aspects = stoull(val);
    else if (key == "mesh_leaf_capacity") o.mesh_leaf_capacity = stoi(val);
    else if (key == "mesh_memory_mb") o.mesh_memory_mb = atof(val.c_str());
    else if (key == "mesh_ef") o.mesh_ef = stoi(val);
    else if (key == "mesh_candidate_budget") o.mesh_candidate_budget = stoi(val);
    else if (key == "mesh_max_candidates") o.mesh_max_candidates = stoi(val);
    else if (key == "mesh_seed") o.mesh_seed = stoull(val);
    else if (key == "mesh_training_tag") o.mesh_training_tag = val;
    else if (key == "mesh_placement_taus") o.mesh_placement_taus = val;
    else if (key == "mesh_frozen_placement_manifest") o.mesh_frozen_placement_manifest = val;
    else if (key == "mesh_index_dir") o.mesh_index_dir = val;
    else if (key == "max_index_vectors") o.max_index_vectors = stoull(val);
    else if (key == "build_ablation_indexes") o.build_ablation_indexes = (stoi(val) != 0);
    else throw runtime_error("Unknown option: " + key);
    return true;
}

static string normalizeCliKey(string key) {
    if (key.rfind("--", 0) == 0) key = key.substr(2);
    replace(key.begin(), key.end(), '-', '_');
    return key;
}

static bool applyCliOption(Options& o, int& i, int argc, char** argv) {
    string arg = argv[i];
    if (arg == "--quiet") return true;
    if (arg.rfind("--", 0) == 0) {
        string key = normalizeCliKey(arg);
        string val = "1";
        if (i + 1 < argc) {
            string next = argv[i + 1];
            if (next.rfind("--", 0) != 0) {
                val = next;
                ++i;
            }
        }
        return applyOption(o, key + "=" + val);
    }
    return applyOption(o, arg);
}

static vector<ExperimentTask> readTaskFile(const string& path) {
    ifstream in(path);
    if (!in.is_open()) throw runtime_error("Cannot open task file: " + path);
    vector<ExperimentTask> tasks;
    string line;
    while (getline(in, line)) {
        if (line.empty()) continue;
        stringstream ss(line);
        string tag, tau, k, gt, pname, pvalue;
        getline(ss, tag, '\t');
        getline(ss, tau, '\t');
        getline(ss, k, '\t');
        getline(ss, gt, '\t');
        getline(ss, pname, '\t');
        getline(ss, pvalue, '\t');
        marsbaseline::stripTrailingCarriageReturn(pvalue);
        if (tag == "query_tag") continue;
        if (!tag.empty() && !tau.empty() && !k.empty() && !gt.empty()) {
            tasks.push_back({tag, atof(tau.c_str()), atoi(k.c_str()), gt, pname, pvalue});
        }
    }
    return tasks;
}

static Options optionsForTask(Options base, const ExperimentTask& task) {
    base.query_tag = task.query_tag;
    base.tau = task.tau;
    base.k = task.k;
    base.gt_path = task.gt_path;
    base.param_name = task.param_name;
    base.param_value = task.param_value;
    if (task.param_name == "budget" || task.param_name == "candidate_budget" || task.param_name == "mars_graph_budget") {
        int budget = max(base.k, atoi(task.param_value.c_str()));
        base.candidate_budget = budget;
        base.max_candidate_budget = budget;
        base.mars_graph_budget = budget;
    }
    if (task.param_name == "mesh_ef") base.mesh_ef = max(base.k, atoi(task.param_value.c_str()));
    if (task.param_name == "mesh_candidate_budget") {
        const int budget = max(base.k, atoi(task.param_value.c_str()));
        base.mesh_candidate_budget = budget;
        base.mesh_max_candidates = budget;
    }
    if (task.param_name == "mesh_memory_mb") base.mesh_memory_mb = atof(task.param_value.c_str());
    return base;
}

static void printTaskStart(
    const string& algo, const Options& opt, size_t task_index = 0, size_t task_total = 0
) {
    cout << "[RUN] " << opt.region << " " << algo;
    if (task_total > 0) cout << " task " << task_index << "/" << task_total;
    cout << " param=" << opt.param_name << "=" << opt.param_value
         << " query_tag=" << opt.query_tag
         << " tau=" << int(opt.tau)
         << " k=" << opt.k << endl;
}

static Options parseArgs(int argc, char** argv) {
    if (argc < 9) {
        cerr << "Usage: " << argv[0]
             << " <REGION> <query_tag> <tau> <k> <groundtruth_tsv> <results_csv> <param_name> <param_value> [max_index_vectors]\n";
        exit(1);
    }
    Options o;
    o.region = argv[1]; o.query_tag = argv[2];
    o.tau = atof(argv[3]); o.k = atoi(argv[4]); o.gt_path = argv[5];
    o.results_csv = argv[6]; o.param_name = argv[7]; o.param_value = argv[8];
    for (int i = 9; i < argc; ++i) {
        string arg = argv[i];
        if (!applyCliOption(o, i, argc, argv)) o.max_index_vectors = stoull(arg);
    }
    return o;
}

static Options parseBatchArgs(int argc, char** argv, string& task_file) {
    if (argc < 5 || string(argv[1]) != "--tasks") {
        cerr << "Usage: " << argv[0]
             << " --tasks <REGION> <task_tsv> <results_csv> [max_index_vectors] [key=value...]\n";
        exit(1);
    }
    Options o;
    o.region = argv[2];
    task_file = argv[3];
    o.results_csv = argv[4];
    for (int i = 5; i < argc; ++i) {
        string arg = argv[i];
        if (!applyCliOption(o, i, argc, argv)) o.max_index_vectors = stoull(arg);
    }
    return o;
}
