#include "../baselines/mars_baselines/baseline_common.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <utility>

static constexpr double PI = 3.14159265358979323846;
static constexpr array<size_t, 7> WORKLOAD_HIST_LIMITS = {32, 128, 512, 1024, 2048, 4096, 5500};

enum class ProcessorKind {
    QuantizedBatchScan,
    SymphonyQG,
    QuantizedANN,
};

enum class MarsVariant {
    Full,
    NoSemanticGroupPruning,
    OnlyQuantizedANN,
    OnlySymphonyQG,
};

struct VertexCoord {
    double x = 0.0;
    double y = 0.0;
    bool present = false;
};

struct RoadBlock {
    int id = 0;
    int level = 0;
    int parent = -1;
    int representative_vertex = -1;  // 1-based vertex id
    vector<int> children;
    vector<int> vertices;  // 1-based vertex ids
    size_t workload = 0;
    double road_radius = 0.0;
    double min_x = 0.0;
    double max_x = 0.0;
    double min_y = 0.0;
    double max_y = 0.0;
    int key_l = -1;
    int key_h = -1;
};

struct RoadBlockHierarchy {
    vector<double> scales;
    vector<VertexCoord> coords;
    vector<vector<RoadBlock>> levels;
    vector<vector<int>> vertex_to_block;  // [level][1-based vertex id] -> block id
    vector<vector<float>> boundary_distance;  // [level][1-based vertex id] -> d(v, nearest boundary of its block)
    vector<int> vertex_key;  // [1-based vertex id] -> DFS key
    vector<int> key_vertex;  // [DFS key] -> 1-based vertex id
};

struct SemanticGroup {
    vector<size_t> aspect_ids;  // vector entries A_{B,g}, ids in MarsIndex::global
    unordered_map<size_t, vector<int>> aspect_to_objects;
    vector<size_t> local_to_aspect;
    vector<float> center;
    vector<uint8_t> quantized_codes;
    vector<float> quantization_errors;
    unique_ptr<rabitqlib::symqg::QuantizedGraph<float>> local_qg;
    unique_ptr<rabitqlib::ivf::IVF> local_ivf;
    size_t workload = 0;
    size_t padded_workload = 0;
    size_t batch_count = 0;
    double radius = 0.0;
    ProcessorKind selected_processor = ProcessorKind::QuantizedBatchScan;
};

struct SemanticProcessor {
    array<SemanticGroup, 11> groups;
};

struct LocalIndexStats {
    size_t potential_block_groups = 0;
    size_t nonempty_block_groups = 0;
    size_t scanned_groups = 0;
    size_t scanned_aspects = 0;
    size_t qg_groups = 0;
    size_t selected_qg_groups = 0;
    size_t ivf_groups = 0;
    size_t qg_aspects = 0;
    size_t selected_qg_aspects = 0;
    size_t ivf_aspects = 0;
    double qg_build_ms = 0.0;
    double selected_qg_build_ms = 0.0;
    double ivf_build_ms = 0.0;
    double qg_index_mb = 0.0;
    double selected_qg_index_mb = 0.0;
    double ivf_index_mb = 0.0;
    array<size_t, WORKLOAD_HIST_LIMITS.size() + 1> workload_hist = {};
};

struct LocalGraphIndex {
    int level = -1;
    int block_id = -1;
    int key_l = -1;
    int key_h = -1;
    vector<size_t> local_to_aspect;
    unique_ptr<rabitqlib::symqg::QuantizedGraph<float>> qg;
    double build_ms = 0.0;
    double index_mb = 0.0;
};

static void recordWorkloadHistogram(LocalIndexStats& stats, size_t workload) {
    for (size_t i = 0; i < WORKLOAD_HIST_LIMITS.size(); ++i) {
        if (workload <= WORKLOAD_HIST_LIMITS[i]) {
            stats.workload_hist[i]++;
            return;
        }
    }
    stats.workload_hist[WORKLOAD_HIST_LIMITS.size()]++;
}

static string workloadHistogramStatus(const LocalIndexStats& stats) {
    ostringstream out;
    for (size_t i = 0; i < WORKLOAD_HIST_LIMITS.size(); ++i) {
        out << ";workload_le" << WORKLOAD_HIST_LIMITS[i] << "=" << stats.workload_hist[i];
    }
    out << ";workload_gt" << WORKLOAD_HIST_LIMITS.back() << "="
        << stats.workload_hist[WORKLOAD_HIST_LIMITS.size()];
    return out.str();
}

struct MarsIndex {
    GlobalIndexData global;
    RoadBlockHierarchy hierarchy;
    vector<vector<SemanticProcessor>> processors;  // [level][block id]
    unique_ptr<rabitqlib::symqg::QuantizedGraph<float>> global_qg;
    vector<vector<LocalGraphIndex>> local_graphs;  // [level][block id], optional V2 path
    LocalIndexStats local_index_stats;
    double global_load_ms = 0.0;
    double hierarchy_build_ms = 0.0;
    double global_qg_build_ms = 0.0;
    double global_qg_index_mb = 0.0;
    double local_graph_build_ms = 0.0;
    double local_graph_index_mb = 0.0;
    size_t local_graph_count = 0;
    size_t local_graph_aspects = 0;
    double processor_build_ms = 0.0;
    double qg_build_ms = 0.0;
    double ivf_build_ms = 0.0;
    double qg_index_mb = 0.0;
    double ivf_index_mb = 0.0;
    double base_index_mb = 0.0;
    double mars_build_ms = 0.0;
    double mars_index_mb = 0.0;
    double symphony_only_build_ms = 0.0;
    double symphony_only_index_mb = 0.0;
    double quantized_ann_build_ms = 0.0;
    double quantized_ann_index_mb = 0.0;
};

struct QueryState {
    priority_queue<Candidate> heap;
    unordered_set<int> evaluated_objects;
    unordered_map<size_t, double> aspect_score_cache;
};

struct MarsQueryStats {
    double visited_blocks = 0.0;
    double inside_blocks = 0.0;
    double partial_blocks = 0.0;
    double leaf_verified_vertices = 0.0;
    double processed_groups = 0.0;
    double pruned_groups = 0.0;
    double scanned_groups = 0.0;
    double indexed_groups = 0.0;
    double scanned_aspects = 0.0;
    double indexed_candidates = 0.0;
    double exact_evaluated_objects = 0.0;
    double final_heap_size = 0.0;
    double start_level = 0.0;
    double compiled_intervals = 0.0;
    double compiled_interval_keys = 0.0;
    double compiled_fringe_vertices = 0.0;
    double route_graph = 0.0;
    double compiled_selectivity = 0.0;
    double local_graphs_used = 0.0;
    double local_graph_aspects = 0.0;
    double local_graph_covered_keys = 0.0;
    double local_graph_uncovered_keys = 0.0;
    double compile_ms = 0.0;
    double graph_filter_ms = 0.0;
    double graph_search_ms = 0.0;
    double candidate_eval_ms = 0.0;
    double road_scan_ms = 0.0;
    double graph_attempts = 0.0;
    double refill_triggered = 0.0;
    double refill_rounds = 0.0;
    double refine_triggered = 0.0;
    double feasible_aspect_checks = 0.0;
    double feasible_category_checks = 0.0;
    double feasible_category_expanded_objects = 0.0;
    double feasible_pointwise_h2h_calls = 0.0;
    double feasible_object_checks = 0.0;
};

struct MarsSearchResult {
    vector<int> ids;
    MarsQueryStats stats;
};

class NullStreamBuffer : public streambuf {
   protected:
    int overflow(int c) override { return c; }
};

class ScopedStreamSilencer {
   public:
    ScopedStreamSilencer() : old_cout_(cout.rdbuf(&null_)), old_cerr_(cerr.rdbuf(&null_)) {}
    ~ScopedStreamSilencer() {
        cout.rdbuf(old_cout_);
        cerr.rdbuf(old_cerr_);
    }

    ScopedStreamSilencer(const ScopedStreamSilencer&) = delete;
    ScopedStreamSilencer& operator=(const ScopedStreamSilencer&) = delete;

   private:
    NullStreamBuffer null_;
    streambuf* old_cout_ = nullptr;
    streambuf* old_cerr_ = nullptr;
};

static size_t objectAspectCount(const ObjectRecord& obj) {
    return 1 + obj.category_ids.size();
}

static double heapTheta(const QueryState& state, int k) {
    return static_cast<int>(state.heap.size()) >= k ? state.heap.top().score : -1e100;
}

static double aspectDotFromGlobal(const vector<float>& q, const GlobalIndexData& global, size_t aspect_id) {
    if (aspect_id >= global.entries.size()) return -1e100;
    const float* row = global.matrix.data() + aspect_id * global.dim;
    double score = 0.0;
    for (size_t d = 0; d < global.dim; ++d) score += double(q[d]) * double(row[d]);
    return score;
}

static double cachedAspectDot(
    QueryState& state,
    const vector<float>& q,
    const GlobalIndexData& global,
    size_t aspect_id
) {
    auto it = state.aspect_score_cache.find(aspect_id);
    if (it != state.aspect_score_cache.end()) return it->second;
    double score = aspectDotFromGlobal(q, global, aspect_id);
    state.aspect_score_cache.emplace(aspect_id, score);
    return score;
}

static double objectScoreFromGlobal(
    const vector<float>& q,
    const ObjectRecord& obj,
    Dataset& ds,
    const GlobalIndexData& global
) {
    double best = -1e100;
    if (obj.object_id >= 0 && obj.object_id < static_cast<int>(global.poi_aspect_by_object.size())) {
        size_t poi_aspect = global.poi_aspect_by_object[obj.object_id];
        best = max(best, aspectDotFromGlobal(q, global, poi_aspect));
    }
    for (int cid : obj.category_ids) {
        auto it = global.cat_aspect_by_id.find(cid);
        if (it != global.cat_aspect_by_id.end()) best = max(best, aspectDotFromGlobal(q, global, it->second));
    }
    if (best < -1e90) return objectScore(q, obj, ds);
    return best;
}

static double objectScoreFromGlobalCached(
    QueryState& state,
    const vector<float>& q,
    const ObjectRecord& obj,
    Dataset& ds,
    const GlobalIndexData& global
) {
    double best = -1e100;
    if (obj.object_id >= 0 && obj.object_id < static_cast<int>(global.poi_aspect_by_object.size())) {
        size_t poi_aspect = global.poi_aspect_by_object[obj.object_id];
        best = max(best, cachedAspectDot(state, q, global, poi_aspect));
    }
    for (int cid : obj.category_ids) {
        auto it = global.cat_aspect_by_id.find(cid);
        if (it != global.cat_aspect_by_id.end()) {
            best = max(best, cachedAspectDot(state, q, global, it->second));
        } else if (cid > 0 && cid <= ds.cat_reader->rows()) {
            vector<float> cv = ds.cat_reader->readNorm1Based(cid);
            best = max(best, dotProduct(q, cv));
        }
    }
    return best;
}

static void pushEvaluatedObject(
    QueryState& state,
    Dataset& ds,
    const GlobalIndexData& global,
    const vector<float>& q,
    int object_id,
    int k,
    MarsQueryStats* stats = nullptr
) {
    if (!state.evaluated_objects.insert(object_id).second) return;
    if (stats) stats->exact_evaluated_objects += 1.0;
    const auto& obj = ds.objects[object_id];
    // F2 scoring-path parity: final object reranking uses the same exact
    // max-over-aspects scorer as all baselines and the shared recall evaluator.
    // The global aspect cache is still used for graph candidate generation, but
    // not for final object scoring, so query-time QPS comparisons are not
    // contaminated by a MARS-only rerank cache.
    (void)global;
    pushTop(state.heap, k, {objectScore(q, obj, ds), obj.object_id, obj.vertex_id});
}

static vector<VertexCoord> loadVertexCoords(const string& region, int road_n) {
    string path = dataPath(region, "USA-road-d." + region + ".co");
    ifstream in(path);
    if (!in.is_open()) throw runtime_error("Cannot open coordinate file: " + path);

    vector<VertexCoord> coords(road_n + 1);
    string line;
    double ref_lat = 0.0;
    int ref_count = 0;
    vector<array<double, 3>> raw;
    while (getline(in, line)) {
        if (line.empty() || line[0] != 'v') continue;
        stringstream ss(line);
        char tag;
        int id;
        double lon_raw, lat_raw;
        ss >> tag >> id >> lon_raw >> lat_raw;
        if (id < 1 || id > road_n) continue;
        double lon = lon_raw / 1000000.0;
        double lat = lat_raw / 1000000.0;
        raw.push_back({double(id), lon, lat});
        ref_lat += lat;
        ref_count++;
    }
    ref_lat = ref_count > 0 ? ref_lat / double(ref_count) : 0.0;
    double cos_lat = cos(ref_lat * PI / 180.0);
    for (const auto& row : raw) {
        int id = int(row[0]);
        coords[id].x = row[1] * 111320.0 * cos_lat;
        coords[id].y = row[2] * 110540.0;
        coords[id].present = true;
    }
    return coords;
}

static size_t vertexWorkload(Dataset& ds, int vertex_id) {
    size_t workload = 0;
    for (int oi : ds.objects_by_vertex[vertex_id]) workload += objectAspectCount(ds.objects[oi]);
    return workload;
}

static void initBlockBounds(RoadBlock& block, const vector<VertexCoord>& coords) {
    bool first = true;
    for (int v : block.vertices) {
        if (v <= 0 || v >= static_cast<int>(coords.size()) || !coords[v].present) continue;
        if (first) {
            block.min_x = block.max_x = coords[v].x;
            block.min_y = block.max_y = coords[v].y;
            first = false;
        } else {
            block.min_x = min(block.min_x, coords[v].x);
            block.max_x = max(block.max_x, coords[v].x);
            block.min_y = min(block.min_y, coords[v].y);
            block.max_y = max(block.max_y, coords[v].y);
        }
    }
    if (first) {
        block.min_x = block.max_x = block.min_y = block.max_y = 0.0;
    }
}

static double mergedSpan(const RoadBlock& a, const RoadBlock& b) {
    double min_x = min(a.min_x, b.min_x);
    double max_x = max(a.max_x, b.max_x);
    double min_y = min(a.min_y, b.min_y);
    double max_y = max(a.max_y, b.max_y);
    double dx = max_x - min_x;
    double dy = max_y - min_y;
    return sqrt(dx * dx + dy * dy);
}

static RoadBlock mergeBlocks(
    int id,
    int level,
    const vector<int>& child_ids,
    const vector<RoadBlock>& prev_level
) {
    RoadBlock block;
    block.id = id;
    block.level = level;
    bool first = true;
    size_t rep_workload = 0;
    for (int child_id : child_ids) {
        const RoadBlock& child = prev_level[child_id];
        block.children.push_back(child_id);
        block.workload += child.workload;
        block.vertices.insert(block.vertices.end(), child.vertices.begin(), child.vertices.end());
        if (block.representative_vertex < 0 || child.workload > rep_workload) {
            block.representative_vertex = child.representative_vertex;
            rep_workload = child.workload;
        }
        if (first) {
            block.min_x = child.min_x;
            block.max_x = child.max_x;
            block.min_y = child.min_y;
            block.max_y = child.max_y;
            first = false;
        } else {
            block.min_x = min(block.min_x, child.min_x);
            block.max_x = max(block.max_x, child.max_x);
            block.min_y = min(block.min_y, child.min_y);
            block.max_y = max(block.max_y, child.max_y);
        }
    }
    for (int child_id : child_ids) {
        const RoadBlock& child = prev_level[child_id];
        if (block.representative_vertex <= 0 || child.representative_vertex <= 0) continue;
        double d = h2hQuery(block.representative_vertex - 1, child.representative_vertex - 1);
        if (d >= 0) block.road_radius = max(block.road_radius, d + child.road_radius);
    }
    return block;
}

static vector<vector<int>> buildBlockAdjacency(
    const vector<RoadBlock>& prev_level,
    const vector<int>& prev_vertex_to_block,
    int road_n
) {
    vector<vector<int>> adj(prev_level.size());
    for (int u = 0; u < road_n; ++u) {
        int bu = prev_vertex_to_block[u + 1];
        for (const ID& e : G[u]) {
            int bv = prev_vertex_to_block[e.first + 1];
            if (bu != bv) adj[bu].push_back(bv);
        }
    }
    for (auto& row : adj) {
        sort(row.begin(), row.end());
        row.erase(unique(row.begin(), row.end()), row.end());
    }
    return adj;
}

static vector<RoadBlock> buildNextHierarchyLevel(
    vector<RoadBlock>& prev_level,
    const vector<vector<int>>& prev_adj,
    double scale,
    int level
) {
    vector<RoadBlock> next_level;
    vector<char> unused(prev_level.size(), 1);
    priority_queue<pair<size_t, int>, vector<pair<size_t, int>>, greater<pair<size_t, int>>> seeds;
    for (const auto& block : prev_level) seeds.push({block.workload, block.id});

    while (!seeds.empty()) {
        int seed = seeds.top().second;
        seeds.pop();
        if (!unused[seed]) continue;

        vector<int> children;
        set<int> frontier;
        children.push_back(seed);
        unused[seed] = 0;
        RoadBlock current = mergeBlocks(static_cast<int>(next_level.size()), level, children, prev_level);
        for (int nb : prev_adj[seed]) {
            if (unused[nb]) frontier.insert(nb);
        }

        while (!frontier.empty()) {
            int best = -1;
            size_t best_workload = numeric_limits<size_t>::max();
            for (int nb : frontier) {
                if (!unused[nb]) continue;
                if (mergedSpan(current, prev_level[nb]) > scale) continue;
                if (prev_level[nb].workload < best_workload) {
                    best = nb;
                    best_workload = prev_level[nb].workload;
                }
            }
            if (best < 0) break;

            frontier.erase(best);
            unused[best] = 0;
            children.push_back(best);
            current = mergeBlocks(static_cast<int>(next_level.size()), level, children, prev_level);
            for (int nb : prev_adj[best]) {
                if (unused[nb]) frontier.insert(nb);
            }
        }

        current.id = static_cast<int>(next_level.size());
        next_level.push_back(std::move(current));
        for (int child_id : children) prev_level[child_id].parent = next_level.back().id;
    }

    return next_level;
}

static void computeBoundaryDistancesForLevel(
    RoadBlockHierarchy& hierarchy,
    int level,
    int road_n
) {
    constexpr float INF = numeric_limits<float>::infinity();
    hierarchy.boundary_distance[level].assign(road_n + 1, INF);

    if (level == 0) {
        for (int v = 1; v <= road_n; ++v) hierarchy.boundary_distance[level][v] = 0.0f;
        return;
    }

    const vector<int>& vertex_to_block = hierarchy.vertex_to_block[level];

    for (const RoadBlock& block : hierarchy.levels[level]) {
        vector<int> boundary_vertices;
        boundary_vertices.reserve(block.vertices.size());
        for (int v1 : block.vertices) {
            if (v1 <= 0 || v1 > road_n) continue;
            bool boundary = false;
            for (const ID& e : G[v1 - 1]) {
                int nb1 = e.first + 1;
                if (nb1 <= 0 || nb1 > road_n || vertex_to_block[nb1] != block.id) {
                    boundary = true;
                    break;
                }
            }
            if (boundary) {
                boundary_vertices.push_back(v1);
            }
        }

        if (boundary_vertices.empty()) continue;

        priority_queue<pair<double, int>, vector<pair<double, int>>, greater<pair<double, int>>> pq;
        for (int v1 : boundary_vertices) {
            hierarchy.boundary_distance[level][v1] = 0.0f;
            pq.push({0.0, v1});
        }

        while (!pq.empty()) {
            auto [d, u1] = pq.top();
            pq.pop();
            if (d > double(hierarchy.boundary_distance[level][u1]) + 1e-6) continue;
            for (const ID& e : G[u1 - 1]) {
                int v1 = e.first + 1;
                if (v1 <= 0 || v1 > road_n || vertex_to_block[v1] != block.id) continue;
                double nd = d + e.second;
                if (nd < double(hierarchy.boundary_distance[level][v1])) {
                    hierarchy.boundary_distance[level][v1] = static_cast<float>(nd);
                    pq.push({nd, v1});
                }
            }
        }
    }
}

static void computeBoundaryDistances(RoadBlockHierarchy& hierarchy, int road_n) {
    cout << "[BUILD] MARS precomputing block boundary distances..." << endl;
    hierarchy.boundary_distance.resize(hierarchy.levels.size());
    for (size_t level = 0; level < hierarchy.levels.size(); ++level) {
        computeBoundaryDistancesForLevel(hierarchy, static_cast<int>(level), road_n);
    }
}

static void assignDfsKeysFromBlock(
    RoadBlockHierarchy& hierarchy,
    int level,
    int block_id,
    int& counter
) {
    RoadBlock& block = hierarchy.levels[level][block_id];
    block.key_l = counter;
    if (level == 0 || block.children.empty()) {
        int vertex = block.vertices.empty() ? -1 : block.vertices.front();
        if (vertex > 0 && vertex < static_cast<int>(hierarchy.vertex_key.size())) {
            hierarchy.vertex_key[vertex] = counter;
            hierarchy.key_vertex.push_back(vertex);
            counter++;
        }
    } else {
        for (int child_id : block.children) {
            assignDfsKeysFromBlock(hierarchy, level - 1, child_id, counter);
        }
    }
    block.key_h = counter - 1;
}

static void assignDfsIntervalLabels(RoadBlockHierarchy& hierarchy, int road_n) {
    cout << "[BUILD] MARS assigning DFS interval labels..." << endl;
    hierarchy.vertex_key.assign(road_n + 1, -1);
    hierarchy.key_vertex.clear();
    if (hierarchy.levels.empty()) return;
    int counter = 0;
    int max_level = static_cast<int>(hierarchy.levels.size()) - 1;
    for (const RoadBlock& block : hierarchy.levels[max_level]) {
        assignDfsKeysFromBlock(hierarchy, max_level, block.id, counter);
    }
}

static RoadBlockHierarchy buildRoadBlockHierarchy(Dataset& ds) {
    RoadBlockHierarchy hierarchy;
    hierarchy.scales = {1000.0, 3000.0, 5000.0, 7000.0, 9000.0};

    hierarchy.coords = loadVertexCoords(ds.region, ds.road_n);
    hierarchy.levels.resize(hierarchy.scales.size() + 1);
    hierarchy.vertex_to_block.resize(hierarchy.scales.size() + 1);

    vector<RoadBlock>& level0 = hierarchy.levels[0];
    level0.reserve(ds.road_n);
    hierarchy.vertex_to_block[0].assign(ds.road_n + 1, -1);
    for (int v = 1; v <= ds.road_n; ++v) {
        RoadBlock block;
        block.id = v - 1;
        block.level = 0;
        block.representative_vertex = v;
        block.vertices.push_back(v);
        block.workload = vertexWorkload(ds, v);
        initBlockBounds(block, hierarchy.coords);
        hierarchy.vertex_to_block[0][v] = block.id;
        level0.push_back(std::move(block));
    }

    for (size_t li = 0; li < hierarchy.scales.size(); ++li) {
        cout << "[BUILD] " << ds.region << " MARS road hierarchy level " << (li + 1)
             << " scale=" << int(hierarchy.scales[li])
             << " prev_blocks=" << hierarchy.levels[li].size() << endl;
        vector<vector<int>> adj = buildBlockAdjacency(
            hierarchy.levels[li], hierarchy.vertex_to_block[li], ds.road_n
        );
        hierarchy.levels[li + 1] = buildNextHierarchyLevel(
            hierarchy.levels[li], adj, hierarchy.scales[li], static_cast<int>(li + 1)
        );
        hierarchy.vertex_to_block[li + 1].assign(ds.road_n + 1, -1);
        for (const auto& block : hierarchy.levels[li + 1]) {
            for (int v : block.vertices) hierarchy.vertex_to_block[li + 1][v] = block.id;
        }
    }

    computeBoundaryDistances(hierarchy, ds.road_n);
    assignDfsIntervalLabels(hierarchy, ds.road_n);
    return hierarchy;
}

static double aspectRadiusToCenter(
    const vector<float>& matrix,
    size_t dim,
    size_t pid,
    const vector<float>& center
) {
    const float* row = matrix.data() + pid * dim;
    double dot = 0.0;
    for (size_t d = 0; d < dim; ++d) dot += double(row[d]) * double(center[d]);
    double sq = max(0.0, 2.0 - 2.0 * dot);
    return sqrt(sq);
}

static void computeSemanticGroupSummary(
    SemanticGroup& group,
    const GlobalIndexData& global,
    const vector<float>& fallback_center
) {
    group.center.clear();
    group.radius = 0.0;
    if (group.aspect_ids.empty() || global.dim == 0) return;

    vector<double> sum(global.dim, 0.0);
    for (size_t aspect_id : group.aspect_ids) {
        if (aspect_id >= global.entries.size()) continue;
        const float* row = global.matrix.data() + aspect_id * global.dim;
        for (size_t d = 0; d < global.dim; ++d) sum[d] += double(row[d]);
    }

    double norm2 = 0.0;
    for (double x : sum) norm2 += x * x;
    group.center.resize(global.dim, 0.0f);
    if (norm2 > 1e-20) {
        double inv_norm = 1.0 / sqrt(norm2);
        for (size_t d = 0; d < global.dim; ++d) group.center[d] = static_cast<float>(sum[d] * inv_norm);
    } else if (fallback_center.size() == global.dim) {
        group.center = fallback_center;
    }

    for (size_t aspect_id : group.aspect_ids) {
        if (aspect_id >= global.entries.size()) continue;
        group.radius = max(group.radius, aspectRadiusToCenter(global.matrix, global.dim, aspect_id, group.center));
    }
}

static void addGroupAspectObject(
    SemanticGroup& group,
    const GlobalIndexData& global,
    size_t aspect_id,
    int object_id
) {
    group.aspect_ids.push_back(aspect_id);
    if (aspect_id < global.entries.size() && global.entries[aspect_id].is_category) {
        group.aspect_to_objects[aspect_id].push_back(object_id);
    }
}

static vector<float> makeLocalAspectMatrix(const GlobalIndexData& global, const vector<size_t>& aspect_ids) {
    vector<float> matrix;
    matrix.resize(aspect_ids.size() * global.dim);
    for (size_t local_id = 0; local_id < aspect_ids.size(); ++local_id) {
        size_t aspect_id = aspect_ids[local_id];
        const float* src = global.matrix.data() + aspect_id * global.dim;
        float* dst = matrix.data() + local_id * global.dim;
        copy(src, src + global.dim, dst);
    }
    return matrix;
}

static double estimateLocalQGIndexMB(size_t n, size_t dim, int degree) {
    return double(n * (dim * sizeof(float) + degree * sizeof(uint32_t) + dim / 8 + 8)) / 1000000.0;
}

static double estimateLocalIVFIndexMB(size_t n, size_t dim, int quant_bits) {
    return double(n * (dim / 8 + dim * max(0, quant_bits - 1) / 8 + sizeof(rabitqlib::PID))) / 1000000.0;
}

static void buildLocalIndexesForGroup(
    SemanticGroup& group,
    const GlobalIndexData& global,
    const vector<float>& centroid,
    const Options& opt,
    LocalIndexStats& stats,
    bool build_qg,
    bool build_ivf,
    bool selected_for_full_mars
) {
    if (group.aspect_ids.empty()) return;
    if (!build_qg && !build_ivf) return;

    group.local_to_aspect = group.aspect_ids;
    vector<float> local_matrix = makeLocalAspectMatrix(global, group.local_to_aspect);
    size_t n = group.local_to_aspect.size();

    if (build_qg && n > static_cast<size_t>(opt.qg_degree)) {
        auto q0 = chrono::high_resolution_clock::now();
        {
            ScopedStreamSilencer silence_library_output;
            group.local_qg = make_unique<rabitqlib::symqg::QuantizedGraph<float>>(
                n, global.dim, opt.qg_degree, rabitqlib::METRIC_IP
            );
            rabitqlib::symqg::QGBuilder builder(*group.local_qg, opt.qg_ef_build, local_matrix.data());
            builder.build();
            group.local_qg->set_ef(opt.ef_search);
        }
        auto q1 = chrono::high_resolution_clock::now();
        double build_ms = chrono::duration<double, milli>(q1 - q0).count();
        double index_mb = estimateLocalQGIndexMB(n, global.dim, opt.qg_degree);
        stats.qg_build_ms += build_ms;
        stats.qg_index_mb += index_mb;
        stats.qg_groups++;
        stats.qg_aspects += n;
        if (selected_for_full_mars) {
            stats.selected_qg_build_ms += build_ms;
            stats.selected_qg_index_mb += index_mb;
            stats.selected_qg_groups++;
            stats.selected_qg_aspects += n;
        }
    }

    if (!build_ivf) return;
    auto i0 = chrono::high_resolution_clock::now();
    vector<rabitqlib::PID> cluster_ids(n, 0);
    {
        ScopedStreamSilencer silence_library_output;
        group.local_ivf = make_unique<rabitqlib::ivf::IVF>(
            n, global.dim, 1, opt.quant_bits, rabitqlib::METRIC_IP
        );
        group.local_ivf->construct(local_matrix.data(), centroid.data(), cluster_ids.data(), true);
    }
    auto i1 = chrono::high_resolution_clock::now();
    stats.ivf_build_ms += chrono::duration<double, milli>(i1 - i0).count();
    stats.ivf_index_mb += estimateLocalIVFIndexMB(n, global.dim, opt.quant_bits);
    stats.ivf_groups++;
    stats.ivf_aspects += n;
}

static void buildQuantizedBatchForGroup(
    SemanticGroup& group,
    const GlobalIndexData& global,
    int quant_bits
) {
    if (group.aspect_ids.empty()) return;
    int bits = max(1, min(8, quant_bits));
    int levels = (1 << bits) - 1;
    double delta = 2.0 / double(levels);
    group.quantized_codes.resize(group.aspect_ids.size() * global.dim, 0);
    group.quantization_errors.resize(group.aspect_ids.size(), 0.0f);

    for (size_t row = 0; row < group.aspect_ids.size(); ++row) {
        size_t aspect_id = group.aspect_ids[row];
        const float* vec = global.matrix.data() + aspect_id * global.dim;
        uint8_t* codes = group.quantized_codes.data() + row * global.dim;
        double err2 = 0.0;
        for (size_t d = 0; d < global.dim; ++d) {
            double clipped = max(-1.0, min(1.0, double(vec[d])));
            int qv = int(llround((clipped + 1.0) / delta));
            qv = max(0, min(levels, qv));
            codes[d] = static_cast<uint8_t>(qv);
            double recon = double(qv) * delta - 1.0;
            double err = double(vec[d]) - recon;
            err2 += err * err;
        }
        group.quantization_errors[row] = static_cast<float>(sqrt(err2));
    }
}

static vector<vector<SemanticProcessor>> buildSemanticProcessors(
    Dataset& ds,
    const GlobalIndexData& global,
    RoadBlockHierarchy& hierarchy,
    const Options& opt,
    LocalIndexStats& stats
) {
    vector<vector<SemanticProcessor>> processors(hierarchy.levels.size());
    for (size_t level = 0; level < hierarchy.levels.size(); ++level) {
        if (level == 0) {
            cout << "[BUILD] " << ds.region << " MARS semantic processors level 0 skipped; leaves use exact vertex verification" << endl;
            continue;
        }
        cout << "[BUILD] " << ds.region << " MARS semantic processors level " << level
             << " blocks=" << hierarchy.levels[level].size() << endl;
        processors[level].resize(hierarchy.levels[level].size());

        for (const auto& obj : ds.objects) {
            if (obj.vertex_id <= 0 || obj.vertex_id >= static_cast<int>(hierarchy.vertex_to_block[level].size())) continue;
            int block_id = hierarchy.vertex_to_block[level][obj.vertex_id];
            if (block_id < 0) continue;

            if (obj.object_id >= 0 && obj.object_id < static_cast<int>(global.poi_aspect_by_object.size())) {
                size_t poi_aspect = global.poi_aspect_by_object[obj.object_id];
                if (poi_aspect != numeric_limits<size_t>::max() && poi_aspect < global.entries.size()) {
                    int g = global.entries[poi_aspect].group_id;
                    SemanticGroup& group = processors[level][block_id].groups[g];
                    addGroupAspectObject(group, global, poi_aspect, obj.object_id);
                }
            }

            int fallback_group = objectGroup(obj, ds.category_map);
            for (int cid : obj.category_ids) {
                auto it = global.cat_aspect_by_id.find(cid);
                if (it == global.cat_aspect_by_id.end()) continue;
                size_t cat_aspect = it->second;
                if (cat_aspect >= global.entries.size()) continue;
                int g = ds.category_map.cat_to_group.count(cid) ? ds.category_map.cat_to_group[cid] : fallback_group;
                SemanticGroup& group = processors[level][block_id].groups[g];
                addGroupAspectObject(group, global, cat_aspect, obj.object_id);
            }
        }

        stats.potential_block_groups += processors[level].size() * 11;
        for (auto& processor : processors[level]) {
            for (int g = 0; g < 11; ++g) {
                auto& group = processor.groups[g];
                sort(group.aspect_ids.begin(), group.aspect_ids.end());
                group.aspect_ids.erase(unique(group.aspect_ids.begin(), group.aspect_ids.end()), group.aspect_ids.end());
                for (auto& row : group.aspect_to_objects) {
                    auto& objects = row.second;
                    sort(objects.begin(), objects.end());
                    objects.erase(unique(objects.begin(), objects.end()), objects.end());
                }
                group.workload = group.aspect_ids.size();
                if (group.workload == 0) continue;
                stats.nonempty_block_groups++;
                recordWorkloadHistogram(stats, group.workload);
                constexpr size_t batch_width = 32;
                group.padded_workload = ((group.workload + batch_width - 1) / batch_width) * batch_width;
                group.batch_count = group.padded_workload / batch_width;
                group.selected_processor = group.workload > static_cast<size_t>(opt.mars_threshold)
                    ? ProcessorKind::SymphonyQG
                    : ProcessorKind::QuantizedBatchScan;
                computeSemanticGroupSummary(group, global, ds.category_map.group_centers[g]);
                buildQuantizedBatchForGroup(group, global, opt.quant_bits);
                bool selected_qg = group.selected_processor == ProcessorKind::SymphonyQG;
                if (!selected_qg) {
                    stats.scanned_groups++;
                    stats.scanned_aspects += group.workload;
                }
                bool build_qg = selected_qg;
                bool build_ivf = opt.build_ablation_indexes && selected_qg;
                buildLocalIndexesForGroup(
                    group,
                    global,
                    group.center.empty() ? ds.category_map.group_centers[g] : group.center,
                    opt,
                    stats,
                    build_qg,
                    build_ivf,
                    selected_qg
                );
            }
        }
    }
    return processors;
}

static double euclideanMinDistanceToBlock(const RoadBlock& block, const VertexCoord& qcoord) {
    double dx = 0.0;
    if (qcoord.x < block.min_x) dx = block.min_x - qcoord.x;
    else if (qcoord.x > block.max_x) dx = qcoord.x - block.max_x;

    double dy = 0.0;
    if (qcoord.y < block.min_y) dy = block.min_y - qcoord.y;
    else if (qcoord.y > block.max_y) dy = qcoord.y - block.max_y;

    return sqrt(dx * dx + dy * dy);
}

static int blockReachabilityStatus(
    const RoadBlock& block,
    const RoadBlockHierarchy& hierarchy,
    int source_vertex,
    double tau
) {
#if STUDENT_DIAGNOSTICS
    ++student_diagnostics.bound_visits;
#endif
    if (source_vertex <= 0 || source_vertex >= static_cast<int>(hierarchy.coords.size())) {
#if STUDENT_DIAGNOSTICS
        ++student_diagnostics.invalid_source;
#endif
        return 1;
    }
    const VertexCoord& qcoord = hierarchy.coords[source_vertex];
    if (qcoord.present && euclideanMinDistanceToBlock(block, qcoord) > tau) {
#if STUDENT_DIAGNOSTICS
        ++student_diagnostics.euclidean_pruned;
#endif
        return 0;
    }
    if (block.representative_vertex <= 0) {
#if STUDENT_DIAGNOSTICS
        ++student_diagnostics.invalid_representative;
#endif
        return 1;
    }
#if STUDENT_DIAGNOSTICS
    ++student_diagnostics.bound_h2h_requested;
#endif
    double d = h2hQuery(source_vertex - 1, block.representative_vertex - 1);
    if (d < 0) return 0;
    if (d - block.road_radius > tau) return 0;   // outside
    if (d + block.road_radius <= tau) return 2;  // inside
    return 1;                                    // partial
}

struct CompiledRoadPredicate {
    vector<pair<int, int>> intervals;
    vector<pair<int, int>> inside_blocks;  // (level, block id), before interval coalescing
    unordered_set<int> fringe_vertices;
    size_t interval_keys = 0;
    MarsQueryStats stats;

    bool containsKey(int key) const {
        if (key < 0) return false;
        size_t lo = 0, hi = intervals.size();
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (intervals[mid].second < key) lo = mid + 1;
            else hi = mid;
        }
        return lo < intervals.size() && intervals[lo].first <= key && key <= intervals[lo].second;
    }

    bool containsVertex(const RoadBlockHierarchy& hierarchy, int vertex_id) const {
        if (vertex_id <= 0 || vertex_id >= static_cast<int>(hierarchy.vertex_key.size())) return false;
        int key = hierarchy.vertex_key[vertex_id];
        return containsKey(key) || fringe_vertices.count(vertex_id) != 0;
    }
};

static void addCompiledInterval(CompiledRoadPredicate& pred, const RoadBlock& block) {
    if (block.key_l < 0 || block.key_h < block.key_l) return;
    pred.intervals.push_back({block.key_l, block.key_h});
}

static void coalesceCompiledIntervals(CompiledRoadPredicate& pred) {
    if (pred.intervals.empty()) return;
    sort(pred.intervals.begin(), pred.intervals.end());
    vector<pair<int, int>> merged;
    merged.reserve(pred.intervals.size());
    for (auto row : pred.intervals) {
        if (merged.empty() || row.first > merged.back().second + 1) {
            merged.push_back(row);
        } else {
            merged.back().second = max(merged.back().second, row.second);
        }
    }
    pred.intervals.swap(merged);
    pred.interval_keys = 0;
    for (const auto& row : pred.intervals) pred.interval_keys += size_t(row.second - row.first + 1);
    pred.stats.compiled_intervals = double(pred.intervals.size());
    pred.stats.compiled_interval_keys = double(pred.interval_keys);
    pred.stats.compiled_fringe_vertices = double(pred.fringe_vertices.size());
}

static void compileRoadPredicateBlock(
    Dataset& ds,
    const RoadBlockHierarchy& hierarchy,
    int level,
    int block_id,
    int source_vertex,
    double tau,
    CompiledRoadPredicate& pred
) {
    const RoadBlock& block = hierarchy.levels[level][block_id];
    pred.stats.visited_blocks += 1.0;
    int status = blockReachabilityStatus(block, hierarchy, source_vertex, tau);
    if (status == 0) return;
    if (status == 2) {
        pred.stats.inside_blocks += 1.0;
        pred.inside_blocks.push_back({level, block_id});
        addCompiledInterval(pred, block);
        return;
    }
    pred.stats.partial_blocks += 1.0;
    if (level <= 1) {
        for (int v : block.vertices) {
            if (v <= 0 || v > ds.road_n) continue;
            if (v >= static_cast<int>(ds.objects_by_vertex.size()) || ds.objects_by_vertex[v].empty()) continue;
            pred.stats.leaf_verified_vertices += 1.0;
            double d = h2hQuery(source_vertex - 1, v - 1);
            if (d >= 0 && d <= tau) pred.fringe_vertices.insert(v);
        }
        return;
    }
    for (int child_id : block.children) {
        compileRoadPredicateBlock(ds, hierarchy, level - 1, child_id, source_vertex, tau, pred);
    }
}

static CompiledRoadPredicate compileRoadPredicate(
    Dataset& ds,
    const RoadBlockHierarchy& hierarchy,
    int source_vertex,
    double tau
) {
    CompiledRoadPredicate pred;
    if (hierarchy.levels.empty()) return pred;
    int max_level = static_cast<int>(hierarchy.levels.size()) - 1;
    for (const RoadBlock& block : hierarchy.levels[max_level]) {
        compileRoadPredicateBlock(ds, hierarchy, max_level, block.id, source_vertex, tau, pred);
    }
    coalesceCompiledIntervals(pred);
    return pred;
}

struct IntervalAspectFeasibilityCache {
    struct SharedState {
        vector<int8_t> memo;
        vector<int8_t> object_memo;
        double aspect_checks = 0.0;
        double category_checks = 0.0;
        double category_expanded_objects = 0.0;
        double pointwise_h2h_calls = 0.0;
        double object_checks = 0.0;
    };

    Dataset& ds;
    const MarsIndex& idx;
    const CompiledRoadPredicate& pred;
    shared_ptr<SharedState> shared;

    IntervalAspectFeasibilityCache(Dataset& ds_, const MarsIndex& idx_, const CompiledRoadPredicate& pred_)
        : ds(ds_),
          idx(idx_),
          pred(pred_),
          shared(make_shared<SharedState>()) {
        shared->memo.assign(idx_.global.entries.size(), -1);
        shared->object_memo.assign(ds_.objects.size(), -1);
    }

    bool objectFeasible(int object_id) const {
        if (object_id < 0 || object_id >= static_cast<int>(ds.objects.size())) return false;
        int8_t& cached = shared->object_memo[object_id];
        if (cached >= 0) return cached != 0;
        shared->object_checks += 1.0;
        bool ok = pred.containsVertex(idx.hierarchy, ds.objects[object_id].vertex_id);
        cached = ok ? 1 : 0;
        return ok;
    }

    bool operator()(rabitqlib::PID aspect_id) {
        if (aspect_id >= idx.global.entries.size()) return false;
        int8_t& cached = shared->memo[aspect_id];
        if (cached >= 0) return cached != 0;
        shared->aspect_checks += 1.0;
        bool ok = false;
        const vector<int>* objects = aspect_id < idx.global.entry_objects.size() ? &idx.global.entry_objects[aspect_id] : nullptr;
        size_t expanded = objects ? objects->size() : 0;
        shared->pointwise_h2h_calls += double(expanded);
        if (aspect_id < idx.global.entries.size() && idx.global.entries[aspect_id].is_category) {
            shared->category_checks += 1.0;
            shared->category_expanded_objects += double(expanded);
        }
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

static void addFeasibilityDiagnostics(MarsQueryStats& stats, const IntervalAspectFeasibilityCache& feasible) {
    stats.feasible_aspect_checks += feasible.shared->aspect_checks;
    stats.feasible_category_checks += feasible.shared->category_checks;
    stats.feasible_category_expanded_objects += feasible.shared->category_expanded_objects;
    stats.feasible_pointwise_h2h_calls += feasible.shared->pointwise_h2h_calls;
    stats.feasible_object_checks += feasible.shared->object_checks;
}

static vector<size_t> collectBlockAspectIds(Dataset& ds, const GlobalIndexData& global, const RoadBlock& block) {
    unordered_set<size_t> seen;
    seen.reserve(block.workload);
    for (int v : block.vertices) {
        if (v <= 0 || v >= static_cast<int>(ds.objects_by_vertex.size())) continue;
        for (int object_id : ds.objects_by_vertex[v]) {
            if (object_id < 0 || object_id >= static_cast<int>(ds.objects.size())) continue;
            const ObjectRecord& obj = ds.objects[object_id];
            if (object_id < static_cast<int>(global.poi_aspect_by_object.size())) {
                size_t aspect_id = global.poi_aspect_by_object[object_id];
                if (aspect_id < global.entries.size()) seen.insert(aspect_id);
            }
            for (int cid : obj.category_ids) {
                auto it = global.cat_aspect_by_id.find(cid);
                if (it != global.cat_aspect_by_id.end()) seen.insert(it->second);
            }
        }
    }
    vector<size_t> ids(seen.begin(), seen.end());
    sort(ids.begin(), ids.end());
    return ids;
}

static void buildLocalGraphIndexes(Dataset& ds, MarsIndex& idx, const Options& opt) {
    if (opt.mars_graph_mode != "local") return;
    if (idx.hierarchy.levels.empty()) return;
    int level = max(0, min(opt.mars_local_graph_level, static_cast<int>(idx.hierarchy.levels.size()) - 1));
    idx.local_graphs.clear();
    idx.local_graphs.resize(idx.hierarchy.levels.size());
    idx.local_graphs[level].resize(idx.hierarchy.levels[level].size());

    cout << "[BUILD] " << opt.region << " MARS building V2 local graphs level=" << level
         << " min_aspects=" << opt.mars_local_graph_min_aspects << endl;
    auto all0 = chrono::high_resolution_clock::now();
    for (const RoadBlock& block : idx.hierarchy.levels[level]) {
        vector<size_t> aspects = collectBlockAspectIds(ds, idx.global, block);
        if (aspects.size() < opt.mars_local_graph_min_aspects) continue;

        LocalGraphIndex graph;
        graph.level = level;
        graph.block_id = block.id;
        graph.key_l = block.key_l;
        graph.key_h = block.key_h;
        graph.local_to_aspect = std::move(aspects);

        vector<float> local_matrix;
        local_matrix.reserve(graph.local_to_aspect.size() * idx.global.dim);
        for (size_t aspect_id : graph.local_to_aspect) {
            const float* row = idx.global.matrix.data() + aspect_id * idx.global.dim;
            local_matrix.insert(local_matrix.end(), row, row + idx.global.dim);
        }

        auto g0 = chrono::high_resolution_clock::now();
        {
            ScopedStreamSilencer silence_library_output;
            graph.qg = make_unique<rabitqlib::symqg::QuantizedGraph<float>>(
                graph.local_to_aspect.size(), idx.global.dim, opt.qg_degree, rabitqlib::METRIC_IP
            );
            rabitqlib::symqg::QGBuilder builder(*graph.qg, opt.qg_ef_build, local_matrix.data());
            builder.build();
            graph.qg->set_ef(opt.ef_search);
        }
        auto g1 = chrono::high_resolution_clock::now();
        graph.build_ms = chrono::duration<double, milli>(g1 - g0).count();
        graph.index_mb = estimateLocalQGIndexMB(graph.local_to_aspect.size(), idx.global.dim, opt.qg_degree);

        idx.local_graph_build_ms += graph.build_ms;
        idx.local_graph_index_mb += graph.index_mb;
        idx.local_graph_aspects += graph.local_to_aspect.size();
        idx.local_graph_count += 1;
        idx.local_graphs[level][block.id] = std::move(graph);
        cout << "[BUILD] " << opt.region << " MARS local graph block level=" << level
             << " id=" << block.id << " aspects=" << idx.local_graphs[level][block.id].local_to_aspect.size()
             << " build_ms=" << idx.local_graphs[level][block.id].build_ms << endl;
    }
    auto all1 = chrono::high_resolution_clock::now();
    double elapsed = chrono::duration<double, milli>(all1 - all0).count();
    cout << "[BUILD] " << opt.region << " MARS V2 local graphs count=" << idx.local_graph_count
         << " aspects=" << idx.local_graph_aspects
         << " qg_ms=" << idx.local_graph_build_ms
         << " elapsed_ms=" << elapsed
         << " mb_est=" << idx.local_graph_index_mb << endl;
}

static void addCoveredInterval(vector<pair<int, int>>& covered, int l, int h) {
    if (l < 0 || h < l) return;
    covered.push_back({l, h});
}

static vector<pair<int, int>> coalescedIntervals(vector<pair<int, int>> rows) {
    if (rows.empty()) return rows;
    sort(rows.begin(), rows.end());
    vector<pair<int, int>> merged;
    for (auto row : rows) {
        if (merged.empty() || row.first > merged.back().second + 1) merged.push_back(row);
        else merged.back().second = max(merged.back().second, row.second);
    }
    return merged;
}

static vector<pair<int, int>> subtractIntervals(
    const vector<pair<int, int>>& base,
    const vector<pair<int, int>>& cover_raw
) {
    vector<pair<int, int>> cover = coalescedIntervals(cover_raw);
    vector<pair<int, int>> out;
    size_t ci = 0;
    for (auto row : base) {
        int cur = row.first;
        while (ci < cover.size() && cover[ci].second < cur) ++ci;
        size_t j = ci;
        while (j < cover.size() && cover[j].first <= row.second) {
            if (cover[j].first > cur) out.push_back({cur, min(row.second, cover[j].first - 1)});
            cur = max(cur, cover[j].second + 1);
            if (cur > row.second) break;
            ++j;
        }
        if (cur <= row.second) out.push_back({cur, row.second});
    }
    return out;
}

static size_t intervalKeyCount(const vector<pair<int, int>>& intervals) {
    size_t total = 0;
    for (const auto& row : intervals) {
        if (row.second >= row.first) total += size_t(row.second - row.first + 1);
    }
    return total;
}

static void processScannedGroup(
    Dataset& ds,
    const MarsIndex& idx,
    QueryState& state,
    const SemanticGroup& group,
    const vector<float>& q,
    const Options& opt,
    MarsQueryStats* stats = nullptr
);

static const vector<int>& groupObjectsForAspect(
    const MarsIndex& idx,
    const SemanticGroup& group,
    size_t aspect_id
) {
    static const vector<int> empty;
    if (aspect_id >= idx.global.entries.size()) return empty;
    const AspectEntry& entry = idx.global.entries[aspect_id];
    if (!entry.is_category) {
        return aspect_id < idx.global.entry_objects.size() ? idx.global.entry_objects[aspect_id] : empty;
    }
    auto it = group.aspect_to_objects.find(aspect_id);
    return it == group.aspect_to_objects.end() ? empty : it->second;
}

template <class SearchIdsFn>
static void processIndexedGroup(
    Dataset& ds,
    const MarsIndex& idx,
    QueryState& state,
    const SemanticGroup& group,
    const vector<float>& q,
    const Options& opt,
    SearchIdsFn search_ids,
    MarsQueryStats* stats = nullptr
) {
    int full_limit = static_cast<int>(min<size_t>(group.local_to_aspect.size(), static_cast<size_t>(INT_MAX)));
    if (full_limit <= 0) {
        processScannedGroup(ds, idx, state, group, q, opt, stats);
        return;
    }
    if (stats) stats->indexed_groups += 1.0;
    int budget = min(full_limit, max(opt.k, opt.candidate_budget));
    int limit = opt.max_candidate_budget > 0
        ? min(full_limit, max(budget, opt.max_candidate_budget))
        : min(full_limit, max(budget, opt.candidate_budget));
    int growth = max(2, opt.candidate_growth);
    size_t before = state.evaluated_objects.size();

    while (true) {
        vector<rabitqlib::PID> ids = search_ids(budget);
        if (stats) stats->indexed_candidates += double(ids.size());
        for (auto local_id : ids) {
            if (local_id >= group.local_to_aspect.size()) continue;
            size_t aspect_id = group.local_to_aspect[local_id];
            if (aspect_id >= idx.global.entries.size()) continue;
            for (int object_id : groupObjectsForAspect(idx, group, aspect_id)) {
                if (object_id < 0 || object_id >= static_cast<int>(ds.objects.size())) continue;
                pushEvaluatedObject(state, ds, idx.global, q, object_id, opt.k, stats);
            }
        }
        if (static_cast<int>(state.heap.size()) >= opt.k || budget >= limit) break;
        if (state.evaluated_objects.size() > before && state.evaluated_objects.size() - before >= static_cast<size_t>(opt.k)) break;
        long long next_budget = 1LL * budget * growth;
        budget = int(min<long long>(limit, next_budget));
    }
}

static void processScannedGroup(
    Dataset& ds,
    const MarsIndex& idx,
    QueryState& state,
    const SemanticGroup& group,
    const vector<float>& q,
    const Options& opt,
    MarsQueryStats* stats
) {
    if (stats) {
        stats->scanned_groups += 1.0;
        stats->scanned_aspects += double(group.workload);
    }
    if (group.quantization_errors.size() != group.aspect_ids.size() ||
        group.quantized_codes.size() != group.aspect_ids.size() * idx.global.dim) {
        for (size_t aspect_id : group.aspect_ids) {
            if (aspect_id >= idx.global.entries.size()) continue;
            for (int object_id : groupObjectsForAspect(idx, group, aspect_id)) {
                if (object_id < 0 || object_id >= static_cast<int>(ds.objects.size())) continue;
                pushEvaluatedObject(state, ds, idx.global, q, object_id, opt.k, stats);
            }
        }
        return;
    }

    int bits = max(1, min(8, opt.quant_bits));
    int levels = (1 << bits) - 1;
    double delta = 2.0 / double(levels);
    unordered_map<int, double> object_ub;

    for (size_t row = 0; row < group.aspect_ids.size(); ++row) {
        size_t aspect_id = group.aspect_ids[row];
        if (aspect_id >= idx.global.entries.size()) continue;

        const uint8_t* codes = group.quantized_codes.data() + row * idx.global.dim;
        double est = 0.0;
        for (size_t d = 0; d < idx.global.dim; ++d) {
            double recon = double(codes[d]) * delta - 1.0;
            est += double(q[d]) * recon;
        }
        double ub = est + double(group.quantization_errors[row]);
        for (int object_id : groupObjectsForAspect(idx, group, aspect_id)) {
            if (object_id < 0 || object_id >= static_cast<int>(ds.objects.size())) continue;
            auto it = object_ub.find(object_id);
            if (it == object_ub.end() || ub > it->second) object_ub[object_id] = ub;
        }
    }

    vector<pair<double, int>> candidates;
    candidates.reserve(object_ub.size());
    for (const auto& row : object_ub) candidates.push_back({row.second, row.first});
    sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    for (const auto& cand : candidates) {
        if (cand.first <= heapTheta(state, opt.k)) break;
        pushEvaluatedObject(state, ds, idx.global, q, cand.second, opt.k, stats);
    }
}

static void processSemanticGroup(
    Dataset& ds,
    MarsIndex& idx,
    QueryState& state,
    const SemanticGroup& group,
    const vector<float>& q,
    const Options& opt,
    MarsVariant variant,
    MarsQueryStats* stats
) {
    if (group.workload == 0) return;
    if (stats) stats->processed_groups += 1.0;

    ProcessorKind processor = group.selected_processor;
    if (variant == MarsVariant::OnlyQuantizedANN) processor = ProcessorKind::QuantizedANN;
    if (variant == MarsVariant::OnlySymphonyQG) processor = ProcessorKind::SymphonyQG;

    if (processor == ProcessorKind::QuantizedBatchScan) {
        processScannedGroup(ds, idx, state, group, q, opt, stats);
    } else if (processor == ProcessorKind::QuantizedANN && group.local_ivf) {
        processIndexedGroup(ds, idx, state, group, q, opt, [&](int budget) {
            vector<rabitqlib::PID> ids(budget);
            group.local_ivf->search(q.data(), budget, 1, ids.data(), true);
            return ids;
        }, stats);
    } else if (processor == ProcessorKind::SymphonyQG && group.local_qg) {
        processIndexedGroup(ds, idx, state, group, q, opt, [&](int budget) {
            vector<rabitqlib::PID> ids(budget);
            group.local_qg->search(q.data(), budget, ids.data());
            return ids;
        }, stats);
    } else {
        processScannedGroup(ds, idx, state, group, q, opt, stats);
    }
}

static double semanticGroupUpperBound(
    Dataset& ds,
    const SemanticGroup& group,
    int group_id,
    const vector<float>& q
) {
    const vector<float>& center = group.center.empty() ? ds.category_map.group_centers[group_id] : group.center;
    return dotProduct(q, center) + group.radius;
}

static void processBlockSemanticProcessor(
    Dataset& ds,
    MarsIndex& idx,
    QueryState& state,
    int level,
    int block_id,
    const vector<float>& q,
    const Options& opt,
    MarsVariant variant,
    MarsQueryStats* stats
) {
    const SemanticProcessor& processor = idx.processors[level][block_id];
    vector<pair<double, int>> ordered_groups;
    ordered_groups.reserve(11);
    for (int g = 0; g < 11; ++g) {
        const SemanticGroup& group = processor.groups[g];
        if (group.workload == 0) continue;
        ordered_groups.push_back({semanticGroupUpperBound(ds, group, g, q), g});
    }
    sort(ordered_groups.begin(), ordered_groups.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    for (const auto& row : ordered_groups) {
        int g = row.second;
        const SemanticGroup& group = processor.groups[g];
        if (variant != MarsVariant::NoSemanticGroupPruning && row.first <= heapTheta(state, opt.k)) {
            if (stats) stats->pruned_groups += 1.0;
            continue;
        }
        processSemanticGroup(ds, idx, state, group, q, opt, variant, stats);
    }
}

static void processPartialLeafVertices(
    Dataset& ds,
    const MarsIndex& idx,
    QueryState& state,
    const RoadBlock& block,
    const vector<float>& q,
    const Options& opt,
    int source_vertex,
    MarsQueryStats* stats
) {
    for (int v : block.vertices) {
        if (v <= 0 || v > ds.road_n) continue;
        if (v >= static_cast<int>(ds.objects_by_vertex.size()) || ds.objects_by_vertex[v].empty()) continue;
        if (stats) stats->leaf_verified_vertices += 1.0;
        double d = h2hQuery(source_vertex - 1, v - 1);
        if (d < 0 || d > opt.tau) continue;
        for (int oi : ds.objects_by_vertex[v]) pushEvaluatedObject(state, ds, idx.global, q, oi, opt.k, stats);
    }
}

static void traverseRoadBlock(
    Dataset& ds,
    MarsIndex& idx,
    QueryState& state,
    int level,
    int block_id,
    const vector<float>& q,
    const Options& opt,
    MarsVariant variant,
    int source_vertex,
    MarsQueryStats* stats
) {
    const RoadBlock& block = idx.hierarchy.levels[level][block_id];
    if (stats) stats->visited_blocks += 1.0;
    int status = blockReachabilityStatus(block, idx.hierarchy, source_vertex, opt.tau);
    if (status == 0) return;
    if (level <= 1) {
        if (stats) stats->partial_blocks += 1.0;
        processPartialLeafVertices(ds, idx, state, block, q, opt, source_vertex, stats);
        return;
    }
    if (status == 2) {
        if (stats) stats->inside_blocks += 1.0;
        processBlockSemanticProcessor(ds, idx, state, level, block_id, q, opt, variant, stats);
        return;
    }
    if (stats) stats->partial_blocks += 1.0;
    for (int child_id : block.children) {
        traverseRoadBlock(ds, idx, state, level - 1, child_id, q, opt, variant, source_vertex, stats);
    }
}

static vector<pair<int, int>> selectInitialTraversalBlocks(
    const RoadBlockHierarchy& hierarchy,
    int source_vertex,
    double tau
) {
    vector<pair<int, int>> starts;
    if (source_vertex <= 0 || hierarchy.levels.empty()) return starts;

    int max_level = static_cast<int>(hierarchy.levels.size()) - 1;
    for (int level = 0; level <= max_level; ++level) {
        if (source_vertex >= static_cast<int>(hierarchy.vertex_to_block[level].size())) continue;
        int block_id = hierarchy.vertex_to_block[level][source_vertex];
        if (block_id < 0) continue;
        double boundary_dist = source_vertex < static_cast<int>(hierarchy.boundary_distance[level].size())
            ? double(hierarchy.boundary_distance[level][source_vertex])
            : -1.0;
        if (boundary_dist < tau) continue;

        const RoadBlock& source_block = hierarchy.levels[level][block_id];
        if (level == 0 || source_block.children.empty()) {
            starts.push_back({level, block_id});
        } else {
            starts.reserve(source_block.children.size());
            for (int child_id : source_block.children) starts.push_back({level - 1, child_id});
        }
        return starts;
    }

    // Conceptual root fallback: root boundary distance is infinity, and its children
    // are the current highest-level blocks. We do not materialize a root processor.
    for (const RoadBlock& block : hierarchy.levels[max_level]) starts.push_back({max_level, block.id});
    return starts;
}

static MarsSearchResult marsSearchDetailed(
    Dataset& ds,
    MarsIndex& idx,
    const Options& opt,
    const vector<float>& q,
    int source_vertex,
    MarsVariant variant
) {
    QueryState state;
    MarsQueryStats stats;
    vector<pair<int, int>> starts = selectInitialTraversalBlocks(idx.hierarchy, source_vertex, opt.tau);
    if (!starts.empty()) stats.start_level = double(starts.front().first);
    for (const auto& start : starts) {
        int level = start.first;
        int block_id = start.second;
        if (level < 0 || level >= static_cast<int>(idx.hierarchy.levels.size())) continue;
        if (block_id < 0 || block_id >= static_cast<int>(idx.hierarchy.levels[level].size())) continue;
        traverseRoadBlock(ds, idx, state, level, block_id, q, opt, variant, source_vertex, &stats);
    }
    stats.final_heap_size = double(state.heap.size());
    return {heapToObjectIds(state.heap), stats};
}

static MarsSearchResult marsCompiledGraphSearchDetailed(
    Dataset& ds,
    MarsIndex& idx,
    const Options& opt,
    const vector<float>& q,
    int source_vertex,
    const CompiledRoadPredicate& pred
);

static void scanCompiledSubset(
    Dataset& ds,
    MarsIndex& idx,
    QueryState& state,
    const Options& opt,
    const vector<float>& q,
    const vector<pair<int, int>>& intervals,
    const unordered_set<int>* fringe_vertices,
    MarsQueryStats& stats
) {
    auto scan_t0 = chrono::high_resolution_clock::now();
    size_t before = state.evaluated_objects.size();
    for (const auto& interval : intervals) {
        for (int key = interval.first; key <= interval.second; ++key) {
            if (key < 0 || key >= static_cast<int>(idx.hierarchy.key_vertex.size())) continue;
            int v = idx.hierarchy.key_vertex[key];
            if (v <= 0 || v >= static_cast<int>(ds.objects_by_vertex.size())) continue;
            for (int oi : ds.objects_by_vertex[v]) pushEvaluatedObject(state, ds, idx.global, q, oi, opt.k, &stats);
        }
    }
    if (fringe_vertices) {
        for (int v : *fringe_vertices) {
            if (v <= 0 || v >= static_cast<int>(ds.objects_by_vertex.size())) continue;
            for (int oi : ds.objects_by_vertex[v]) pushEvaluatedObject(state, ds, idx.global, q, oi, opt.k, &stats);
        }
    }
    size_t scanned = state.evaluated_objects.size() - before;
    if (!intervals.empty() || (fringe_vertices && !fringe_vertices->empty())) {
        stats.scanned_groups += 1.0;
        stats.scanned_aspects += double(scanned);
    }
    auto scan_t1 = chrono::high_resolution_clock::now();
    stats.road_scan_ms += chrono::duration<double, milli>(scan_t1 - scan_t0).count();
}

static bool localGraphReady(const MarsIndex& idx, int level, int block_id) {
    if (level < 0 || level >= static_cast<int>(idx.local_graphs.size())) return false;
    if (block_id < 0 || block_id >= static_cast<int>(idx.local_graphs[level].size())) return false;
    return idx.local_graphs[level][block_id].qg != nullptr;
}

static void collectLocalGraphCandidate(
    const MarsIndex& idx,
    int level,
    int block_id,
    unordered_set<long long>& seen,
    vector<const LocalGraphIndex*>& out,
    vector<pair<int, int>>& covered
) {
    if (!localGraphReady(idx, level, block_id)) return;
    const LocalGraphIndex& graph = idx.local_graphs[level][block_id];
    long long key = (static_cast<long long>(level) << 32) | static_cast<unsigned int>(block_id);
    if (!seen.insert(key).second) return;
    out.push_back(&graph);
    addCoveredInterval(covered, graph.key_l, graph.key_h);
}

static vector<const LocalGraphIndex*> selectLocalGraphsForPredicate(
    const MarsIndex& idx,
    const CompiledRoadPredicate& pred,
    const Options& opt,
    vector<pair<int, int>>& covered
) {
    vector<const LocalGraphIndex*> selected;
    if (opt.mars_graph_mode != "local" || idx.local_graphs.empty()) return selected;
    int target_level = max(0, min(opt.mars_local_graph_level, static_cast<int>(idx.hierarchy.levels.size()) - 1));
    unordered_set<long long> seen;

    for (const auto& ref : pred.inside_blocks) {
        int level = ref.first;
        int block_id = ref.second;
        if (level < 0 || level >= static_cast<int>(idx.hierarchy.levels.size())) continue;
        if (block_id < 0 || block_id >= static_cast<int>(idx.hierarchy.levels[level].size())) continue;
        const RoadBlock& block = idx.hierarchy.levels[level][block_id];

        collectLocalGraphCandidate(idx, level, block_id, seen, selected, covered);

        if (level > target_level && target_level < static_cast<int>(idx.local_graphs.size())) {
            for (const LocalGraphIndex& graph : idx.local_graphs[target_level]) {
                if (!graph.qg) continue;
                if (graph.key_l >= block.key_l && graph.key_h <= block.key_h) {
                    collectLocalGraphCandidate(idx, target_level, graph.block_id, seen, selected, covered);
                }
            }
        } else if (level < target_level && target_level < static_cast<int>(idx.hierarchy.vertex_to_block.size())) {
            int rep = block.representative_vertex;
            if (rep > 0 && rep < static_cast<int>(idx.hierarchy.vertex_to_block[target_level].size())) {
                int ancestor_id = idx.hierarchy.vertex_to_block[target_level][rep];
                collectLocalGraphCandidate(idx, target_level, ancestor_id, seen, selected, covered);
            }
        }
    }

    covered = coalescedIntervals(covered);
    return selected;
}

static MarsSearchResult marsCompiledLocalGraphSearchDetailed(
    Dataset& ds,
    MarsIndex& idx,
    const Options& opt,
    const vector<float>& q,
    int source_vertex,
    const CompiledRoadPredicate& pred
) {
    vector<pair<int, int>> covered;
    vector<const LocalGraphIndex*> graphs = selectLocalGraphsForPredicate(idx, pred, opt, covered);
    if (graphs.empty()) {
        return marsCompiledGraphSearchDetailed(ds, idx, opt, q, source_vertex, pred);
    }

    QueryState state;
    MarsQueryStats stats = pred.stats;
    auto feasible_t0 = chrono::high_resolution_clock::now();
    IntervalAspectFeasibilityCache feasible(ds, idx, pred);
    auto feasible_t1 = chrono::high_resolution_clock::now();
    stats.graph_filter_ms += chrono::duration<double, milli>(feasible_t1 - feasible_t0).count();

    for (const LocalGraphIndex* graph : graphs) {
        if (!graph || !graph->qg) continue;
        int full_limit = static_cast<int>(min<size_t>(graph->local_to_aspect.size(), static_cast<size_t>(INT_MAX)));
        if (full_limit <= 0) continue;
        int graph_budget = opt.mars_graph_budget > 0 ? opt.mars_graph_budget : opt.ef_search;
        int budget = min(full_limit, max(opt.k, graph_budget));
        int limit = opt.max_candidate_budget > 0 ? min(full_limit, max(budget, opt.max_candidate_budget)) : budget;
        int growth = max(2, opt.candidate_growth);

        stats.local_graphs_used += 1.0;
        stats.local_graph_aspects += double(graph->local_to_aspect.size());
        stats.indexed_groups += 1.0;

        while (true) {
            vector<rabitqlib::PID> ids(budget, static_cast<rabitqlib::PID>(graph->local_to_aspect.size()));
            graph->qg->set_ef(static_cast<size_t>(max(opt.ef_search, budget)));
            auto accept = [&](rabitqlib::PID local_id) {
                if (local_id >= graph->local_to_aspect.size()) return false;
                size_t aspect_id = graph->local_to_aspect[local_id];
                return feasible(aspect_id);
            };
            auto search_t0 = chrono::high_resolution_clock::now();
            graph->qg->search_filtered(q.data(), budget, ids.data(), accept);
            auto search_t1 = chrono::high_resolution_clock::now();
            stats.graph_search_ms += chrono::duration<double, milli>(search_t1 - search_t0).count();
            stats.indexed_candidates += double(ids.size());

            auto eval_t0 = chrono::high_resolution_clock::now();
            for (auto local_id : ids) {
                if (local_id >= graph->local_to_aspect.size()) continue;
                size_t aspect_id = graph->local_to_aspect[local_id];
                if (aspect_id >= idx.global.entry_objects.size()) continue;
                for (int object_id : idx.global.entry_objects[aspect_id]) {
                    if (!feasible.objectFeasible(object_id)) continue;
                    pushEvaluatedObject(state, ds, idx.global, q, object_id, opt.k, &stats);
                }
            }
            auto eval_t1 = chrono::high_resolution_clock::now();
            stats.candidate_eval_ms += chrono::duration<double, milli>(eval_t1 - eval_t0).count();

            if (static_cast<int>(state.heap.size()) >= opt.k || budget >= limit) break;
            long long next_budget = 1LL * budget * growth;
            budget = int(min<long long>(limit, next_budget));
        }
    }

    vector<pair<int, int>> uncovered = subtractIntervals(pred.intervals, covered);
    stats.local_graph_covered_keys = double(intervalKeyCount(covered));
    stats.local_graph_uncovered_keys = double(intervalKeyCount(uncovered));
    scanCompiledSubset(ds, idx, state, opt, q, uncovered, &pred.fringe_vertices, stats);

    stats.final_heap_size = double(state.heap.size());
    return {heapToObjectIds(state.heap), stats};
}

static MarsSearchResult marsCompiledGraphSearchDetailed(
    Dataset& ds,
    MarsIndex& idx,
    const Options& opt,
    const vector<float>& q,
    int source_vertex,
    const CompiledRoadPredicate& pred
) {
    QueryState state;
    MarsQueryStats stats = pred.stats;
    if (!idx.global_qg) {
        return marsSearchDetailed(ds, idx, opt, q, source_vertex, MarsVariant::Full);
    }

    int full_limit = static_cast<int>(min<size_t>(idx.global.entries.size(), static_cast<size_t>(INT_MAX)));
    int graph_budget = opt.mars_graph_budget > 0 ? opt.mars_graph_budget : opt.ef_search;
    int budget = min(full_limit, max(opt.k, graph_budget));
    int limit = opt.max_candidate_budget > 0 ? min(full_limit, max(budget, opt.max_candidate_budget)) : budget;
    int growth = max(2, opt.candidate_growth);

    auto feasible_t0 = chrono::high_resolution_clock::now();
    IntervalAspectFeasibilityCache feasible(ds, idx, pred);
    auto feasible_t1 = chrono::high_resolution_clock::now();
    stats.graph_filter_ms += chrono::duration<double, milli>(feasible_t1 - feasible_t0).count();

    while (true) {
        vector<rabitqlib::PID> ids(budget, static_cast<rabitqlib::PID>(idx.global.entries.size()));
        idx.global_qg->set_ef(static_cast<size_t>(max(opt.ef_search, budget)));
        auto search_t0 = chrono::high_resolution_clock::now();
        idx.global_qg->search_filtered(q.data(), budget, ids.data(), feasible);
        auto search_t1 = chrono::high_resolution_clock::now();
        stats.graph_search_ms += chrono::duration<double, milli>(search_t1 - search_t0).count();
        stats.graph_attempts += 1.0;
        stats.indexed_groups += 1.0;
        stats.indexed_candidates += double(ids.size());

        auto eval_t0 = chrono::high_resolution_clock::now();
        for (auto aspect_id : ids) {
            if (aspect_id >= idx.global.entries.size()) continue;
            const vector<int>* objects = aspect_id < idx.global.entry_objects.size()
                ? &idx.global.entry_objects[aspect_id]
                : nullptr;
            if (!objects) continue;
            for (int object_id : *objects) {
                if (!feasible.objectFeasible(object_id)) continue;
                pushEvaluatedObject(state, ds, idx.global, q, object_id, opt.k, &stats);
            }
        }
        auto eval_t1 = chrono::high_resolution_clock::now();
        stats.candidate_eval_ms += chrono::duration<double, milli>(eval_t1 - eval_t0).count();

        if (static_cast<int>(state.heap.size()) >= opt.k || budget >= limit) break;
        long long next_budget = 1LL * budget * growth;
        budget = int(min<long long>(limit, next_budget));
    }
    addFeasibilityDiagnostics(stats, feasible);

    bool needs_refill = opt.mars_graph_refill_scan && static_cast<int>(state.heap.size()) < opt.k;
    bool needs_refine = opt.mars_graph_refine_selectivity > 0.0
        && pred.stats.compiled_selectivity <= opt.mars_graph_refine_selectivity;
    if (needs_refill || needs_refine) {
        if (needs_refill) {
            stats.refill_triggered += 1.0;
            stats.refill_rounds += 1.0;
        }
        if (needs_refine) stats.refine_triggered += 1.0;
        scanCompiledSubset(ds, idx, state, opt, q, pred.intervals, &pred.fringe_vertices, stats);
    }
    stats.final_heap_size = double(state.heap.size());
    return {heapToObjectIds(state.heap), stats};
}

static MarsSearchResult marsCompiledRoadScanDetailed(
    Dataset& ds,
    MarsIndex& idx,
    const Options& opt,
    const vector<float>& q,
    const CompiledRoadPredicate& pred
) {
    QueryState state;
    MarsQueryStats stats = pred.stats;
    scanCompiledSubset(ds, idx, state, opt, q, pred.intervals, &pred.fringe_vertices, stats);
    stats.final_heap_size = double(state.heap.size());
    return {heapToObjectIds(state.heap), stats};
}

static MarsSearchResult marsCompiledGraphFirstFallbackDetailed(
    Dataset& ds,
    MarsIndex& idx,
    const Options& opt,
    const vector<float>& q,
    int source_vertex,
    const CompiledRoadPredicate& pred
) {
    QueryState state;
    MarsQueryStats stats = pred.stats;
    if (!idx.global_qg) {
        scanCompiledSubset(ds, idx, state, opt, q, pred.intervals, &pred.fringe_vertices, stats);
        stats.final_heap_size = double(state.heap.size());
        return {heapToObjectIds(state.heap), stats};
    }

    int full_limit = static_cast<int>(min<size_t>(idx.global.entries.size(), static_cast<size_t>(INT_MAX)));
    int graph_budget = opt.mars_graph_budget > 0 ? opt.mars_graph_budget : opt.ef_search;
    int budget = min(full_limit, max(opt.k, graph_budget));

    auto feasible_t0 = chrono::high_resolution_clock::now();
    IntervalAspectFeasibilityCache feasible(ds, idx, pred);
    auto feasible_t1 = chrono::high_resolution_clock::now();
    stats.graph_filter_ms += chrono::duration<double, milli>(feasible_t1 - feasible_t0).count();

    vector<rabitqlib::PID> ids(budget, static_cast<rabitqlib::PID>(idx.global.entries.size()));
    idx.global_qg->set_ef(static_cast<size_t>(max(opt.ef_search, budget)));
    auto search_t0 = chrono::high_resolution_clock::now();
    idx.global_qg->search_filtered(q.data(), budget, ids.data(), feasible);
    auto search_t1 = chrono::high_resolution_clock::now();
    stats.graph_search_ms += chrono::duration<double, milli>(search_t1 - search_t0).count();
    stats.graph_attempts += 1.0;
    stats.indexed_groups += 1.0;
    stats.indexed_candidates += double(ids.size());

    auto eval_t0 = chrono::high_resolution_clock::now();
    for (auto aspect_id : ids) {
        if (aspect_id >= idx.global.entries.size()) continue;
        const vector<int>* objects = aspect_id < idx.global.entry_objects.size()
            ? &idx.global.entry_objects[aspect_id]
            : nullptr;
        if (!objects) continue;
        for (int object_id : *objects) {
            if (!feasible.objectFeasible(object_id)) continue;
            pushEvaluatedObject(state, ds, idx.global, q, object_id, opt.k, &stats);
        }
    }
    auto eval_t1 = chrono::high_resolution_clock::now();
    stats.candidate_eval_ms += chrono::duration<double, milli>(eval_t1 - eval_t0).count();
    addFeasibilityDiagnostics(stats, feasible);

    bool needs_refill = static_cast<int>(state.heap.size()) < opt.k;
    bool needs_refine = opt.mars_graph_refine_selectivity > 0.0
        && pred.stats.compiled_selectivity <= opt.mars_graph_refine_selectivity;
    if (needs_refill || needs_refine) {
        if (needs_refill) {
            stats.refill_triggered += 1.0;
            stats.refill_rounds += 1.0;
        }
        if (needs_refine) stats.refine_triggered += 1.0;
        scanCompiledSubset(ds, idx, state, opt, q, pred.intervals, &pred.fringe_vertices, stats);
    }
    stats.final_heap_size = double(state.heap.size());
    return {heapToObjectIds(state.heap), stats};
}

static MarsSearchResult marsCompileThenSearchDetailed(
    Dataset& ds,
    MarsIndex& idx,
    const Options& opt,
    const vector<float>& q,
    int source_vertex
) {
    auto compile_t0 = chrono::high_resolution_clock::now();
    CompiledRoadPredicate pred = compileRoadPredicate(ds, idx.hierarchy, source_vertex, opt.tau);
    auto compile_t1 = chrono::high_resolution_clock::now();
    pred.stats.compile_ms = chrono::duration<double, milli>(compile_t1 - compile_t0).count();
    double selectivity = ds.road_n > 0
        ? (double(pred.interval_keys + pred.fringe_vertices.size()) / double(ds.road_n))
        : 1.0;
    pred.stats.compiled_selectivity = selectivity;
    if (selectivity >= opt.mars_graph_selectivity) {
        pred.stats.route_graph = 1.0;
        if (opt.mars_graph_mode == "local") {
            return marsCompiledLocalGraphSearchDetailed(ds, idx, opt, q, source_vertex, pred);
        }
        return marsCompiledGraphSearchDetailed(ds, idx, opt, q, source_vertex, pred);
    }
    pred.stats.route_graph = 0.0;
    if (opt.mars_graph_first_fallback) {
        pred.stats.route_graph = 1.0;
        return marsCompiledGraphFirstFallbackDetailed(ds, idx, opt, q, source_vertex, pred);
    }
    return marsCompiledRoadScanDetailed(ds, idx, opt, q, pred);
}

static MarsIndex buildMarsIndex(Dataset& ds, const Options& opt) {
    cout << "[BUILD] " << opt.region << " MARS building H2H road index..." << endl;
    buildH2HIndex(opt.region);

    MarsIndex idx;
    auto build_start = chrono::high_resolution_clock::now();

    cout << "[BUILD] " << opt.region << " MARS loading global aspect float matrix..." << endl;
    auto t0 = chrono::high_resolution_clock::now();
    idx.global = buildGlobalIndexData(ds, opt.max_index_vectors);
    auto t1 = chrono::high_resolution_clock::now();
    idx.global_load_ms = chrono::duration<double, milli>(t1 - t0).count();
    double raw_mb = double(idx.global.matrix.size() * sizeof(float)) / 1000000.0;
    cout << "[BUILD] " << opt.region << " MARS aspects=" << idx.global.entries.size()
         << " dim=" << idx.global.dim << " raw_matrix_mb=" << raw_mb
         << " load_ms=" << idx.global_load_ms << endl;

    cout << "[BUILD] " << opt.region << " MARS building workload-first road hierarchy..." << endl;
    auto h0 = chrono::high_resolution_clock::now();
    idx.hierarchy = buildRoadBlockHierarchy(ds);
    auto h1 = chrono::high_resolution_clock::now();
    idx.hierarchy_build_ms = chrono::duration<double, milli>(h1 - h0).count();

    cout << "[BUILD] " << opt.region << " MARS building global SymphonyQG graph..." << endl;
    auto gq0 = chrono::high_resolution_clock::now();
    {
        ScopedStreamSilencer silence_library_output;
        idx.global_qg = make_unique<rabitqlib::symqg::QuantizedGraph<float>>(
            idx.global.entries.size(), idx.global.dim, opt.qg_degree, rabitqlib::METRIC_IP
        );
        rabitqlib::symqg::QGBuilder builder(*idx.global_qg, opt.qg_ef_build, idx.global.matrix.data());
        builder.build();
        idx.global_qg->set_ef(opt.ef_search);
    }
    auto gq1 = chrono::high_resolution_clock::now();
    idx.global_qg_build_ms = chrono::duration<double, milli>(gq1 - gq0).count();
    idx.global_qg_index_mb = estimateLocalQGIndexMB(idx.global.entries.size(), idx.global.dim, opt.qg_degree);
    buildLocalGraphIndexes(ds, idx, opt);
    idx.qg_build_ms = idx.global_qg_build_ms;
    idx.qg_index_mb = idx.global_qg_index_mb + idx.local_graph_index_mb;
    idx.ivf_build_ms = 0.0;
    idx.ivf_index_mb = 0.0;
    LocalIndexStats local_stats;
    idx.local_index_stats = local_stats;

    double hierarchy_mb = 0.0;
    for (const auto& level : idx.hierarchy.levels) {
        for (const auto& block : level) hierarchy_mb += double(block.vertices.size() * sizeof(int) + block.children.size() * sizeof(int));
    }
    for (const auto& level_dist : idx.hierarchy.boundary_distance) {
        hierarchy_mb += double(level_dist.size() * sizeof(float));
    }
    double processor_mb = 0.0;
    processor_mb = 0.0;
    hierarchy_mb /= 1000000.0;
    processor_mb /= 1000000.0;
    idx.base_index_mb = raw_mb + hierarchy_mb + processor_mb;
    idx.mars_index_mb = idx.base_index_mb + idx.global_qg_index_mb + idx.local_graph_index_mb;
    idx.symphony_only_index_mb = idx.mars_index_mb;
    idx.quantized_ann_index_mb = idx.base_index_mb;

    auto build_end = chrono::high_resolution_clock::now();
    double processor_base_ms = 0.0;
    idx.processor_build_ms = 0.0;
    idx.mars_build_ms = idx.global_load_ms + idx.hierarchy_build_ms + idx.global_qg_build_ms + idx.local_graph_build_ms;
    idx.symphony_only_build_ms = idx.mars_build_ms;
    idx.quantized_ann_build_ms = idx.global_load_ms + idx.hierarchy_build_ms;
    double runner_build_ms = chrono::duration<double, milli>(build_end - build_start).count();
    cout << "MARS index summary: aspects=" << idx.global.entries.size()
         << " hierarchy_ms=" << idx.hierarchy_build_ms
         << " processor_ms=" << idx.processor_build_ms
         << " processor_base_ms=" << processor_base_ms
         << " qg_ms=" << idx.qg_build_ms
         << " local_graph_ms=" << idx.local_graph_build_ms
         << " ivf_ms=" << idx.ivf_build_ms
         << " potential_block_groups=" << local_stats.potential_block_groups
         << " nonempty_block_groups=" << local_stats.nonempty_block_groups
         << " scanned_groups=" << local_stats.scanned_groups
         << " workload_hist=" << workloadHistogramStatus(local_stats).substr(1)
         << " local_qg_groups=" << local_stats.qg_groups
         << " selected_qg_groups=" << local_stats.selected_qg_groups
         << " local_ivf_groups=" << local_stats.ivf_groups
         << " scanned_aspects=" << local_stats.scanned_aspects
         << " local_qg_aspects=" << local_stats.qg_aspects
         << " local_graph_count=" << idx.local_graph_count
         << " local_graph_aspects=" << idx.local_graph_aspects
         << " selected_qg_aspects=" << local_stats.selected_qg_aspects
         << " local_ivf_aspects=" << local_stats.ivf_aspects
         << " runner_build_ms=" << runner_build_ms
         << " mars_build_ms=" << idx.mars_build_ms
         << " symphony_only_build_ms=" << idx.symphony_only_build_ms
         << " quantized_ann_build_ms=" << idx.quantized_ann_build_ms
         << " hierarchy_mb_est=" << hierarchy_mb
         << " processor_mb_est=" << processor_mb
         << " base_index_mb_est=" << idx.base_index_mb
         << " mars_index_mb_est=" << idx.mars_index_mb
         << " local_graph_mb_est=" << idx.local_graph_index_mb
         << " symphony_only_index_mb_est=" << idx.symphony_only_index_mb
         << " quantized_ann_index_mb_est=" << idx.quantized_ann_index_mb << endl;
    return idx;
}

static pair<size_t, size_t> marsIndexedStats(const MarsIndex& idx, MarsVariant variant) {
    const LocalIndexStats& s = idx.local_index_stats;
    size_t indexed_groups = s.selected_qg_groups;
    size_t indexed_aspects = s.selected_qg_aspects;
    if (variant == MarsVariant::OnlySymphonyQG) {
        indexed_groups = s.qg_groups;
        indexed_aspects = s.qg_aspects;
    } else if (variant == MarsVariant::OnlyQuantizedANN) {
        indexed_groups = s.ivf_groups;
        indexed_aspects = s.ivf_aspects;
    }
    return {indexed_groups, indexed_aspects};
}

static string marsIndexStatus(const MarsIndex& idx) {
    const LocalIndexStats& s = idx.local_index_stats;
    return "potential_block_groups=" + to_string(s.potential_block_groups)
        + ";nonempty_block_groups=" + to_string(s.nonempty_block_groups)
        + ";scanned_groups=" + to_string(s.scanned_groups)
        + ";scanned_aspects=" + to_string(s.scanned_aspects)
        + ";selected_qg_groups=" + to_string(s.selected_qg_groups)
        + ";selected_qg_aspects=" + to_string(s.selected_qg_aspects)
        + ";all_qg_groups=" + to_string(s.qg_groups)
        + ";all_qg_aspects=" + to_string(s.qg_aspects)
        + ";ivf_groups=" + to_string(s.ivf_groups)
        + ";ivf_aspects=" + to_string(s.ivf_aspects)
        + ";local_graph_count=" + to_string(idx.local_graph_count)
        + ";local_graph_aspects=" + to_string(idx.local_graph_aspects)
        + ";local_graph_build_ms=" + to_string(idx.local_graph_build_ms)
        + ";local_graph_index_mb=" + to_string(idx.local_graph_index_mb)
        + workloadHistogramStatus(s);
}

static double marsStatValue(const MarsQueryStats& s, int field) {
    switch (field) {
        case 0: return s.visited_blocks;
        case 1: return s.inside_blocks;
        case 2: return s.partial_blocks;
        case 3: return s.leaf_verified_vertices;
        case 4: return s.processed_groups;
        case 5: return s.pruned_groups;
        case 6: return s.scanned_groups;
        case 7: return s.indexed_groups;
        case 8: return s.scanned_aspects;
        case 9: return s.indexed_candidates;
        case 10: return s.exact_evaluated_objects;
        case 11: return s.final_heap_size;
        case 12: return s.start_level;
        case 13: return s.compiled_intervals;
        case 14: return s.compiled_interval_keys;
        case 15: return s.compiled_fringe_vertices;
        case 16: return s.route_graph;
        case 17: return s.compiled_selectivity;
        case 18: return s.local_graphs_used;
        case 19: return s.local_graph_aspects;
        case 20: return s.local_graph_covered_keys;
        case 21: return s.local_graph_uncovered_keys;
        case 22: return s.compile_ms;
        case 23: return s.graph_filter_ms;
        case 24: return s.graph_search_ms;
        case 25: return s.candidate_eval_ms;
        case 26: return s.road_scan_ms;
        case 27: return s.graph_attempts;
        case 28: return s.refill_triggered;
        case 29: return s.refill_rounds;
        case 30: return s.refine_triggered;
        case 31: return s.feasible_aspect_checks;
        case 32: return s.feasible_category_checks;
        case 33: return s.feasible_category_expanded_objects;
        case 34: return s.feasible_pointwise_h2h_calls;
        case 35: return s.feasible_object_checks;
        default: return 0.0;
    }
}

static string marsDiagnosticsStatus(const vector<MarsQueryStats>& stats) {
    static const vector<string> names = {
        "visited_blocks",
        "inside_blocks",
        "partial_blocks",
        "leaf_verified_vertices",
        "processed_groups",
        "pruned_groups",
        "scanned_groups",
        "indexed_groups",
        "scanned_aspects",
        "indexed_candidates",
        "exact_evaluated_objects",
        "final_heap_size",
        "start_level",
        "compiled_intervals",
        "compiled_interval_keys",
        "compiled_fringe_vertices",
        "route_graph",
        "compiled_selectivity",
        "local_graphs_used",
        "local_graph_aspects",
        "local_graph_covered_keys",
        "local_graph_uncovered_keys",
        "compile_ms",
        "graph_filter_ms",
        "graph_search_ms",
        "candidate_eval_ms",
        "road_scan_ms",
        "graph_attempts",
        "refill_triggered",
        "refill_rounds",
        "refine_triggered",
        "feasible_aspect_checks",
        "feasible_category_checks",
        "feasible_category_expanded_objects",
        "feasible_pointwise_h2h_calls",
        "feasible_object_checks",
    };
    if (stats.empty()) return "";

    ostringstream out;
    out << fixed << setprecision(3);
    for (size_t field = 0; field < names.size(); ++field) {
        vector<double> values;
        values.reserve(stats.size());
        double sum = 0.0;
        for (const auto& row : stats) {
            double v = marsStatValue(row, static_cast<int>(field));
            values.push_back(v);
            sum += v;
        }
        out << ";diag_avg_" << names[field] << "=" << (sum / double(values.size()))
            << ";diag_p95_" << names[field] << "=" << percentile(values, 0.95);
    }
    return out.str();
}

template <class SearchFn>
static void runMarsQueries(
    const Options& opt,
    Dataset& ds,
    const string& algo,
    SearchFn search_fn,
    double build_ms,
    double index_mb,
    size_t indexed_block_groups,
    size_t indexed_aspects,
    const string& status_extra
) {
    vector<double> times;
    vector<MarsQueryStats> query_stats;
    double recall_score_sum = 0.0;
    double recall_id_sum = 0.0;
    times.reserve(ds.query_vecs.size());
    query_stats.reserve(ds.query_vecs.size());

    for (size_t qi = 0; qi < ds.query_vecs.size(); ++qi) {
        auto t0 = chrono::high_resolution_clock::now();
        MarsSearchResult pred = search_fn(ds.query_vecs[qi], ds.query_meta[qi].source_vertex_id);
        auto t1 = chrono::high_resolution_clock::now();
        times.push_back(chrono::duration<double, milli>(t1 - t0).count());
        query_stats.push_back(pred.stats);
        recall_score_sum += recallAtScoreThreshold(pred.ids, ds.query_vecs[qi], ds, ds.query_meta[qi].query_id, opt.k);
        recall_id_sum += recallAtIds(pred.ids, ds, ds.query_meta[qi].query_id, opt.k);
    }

    double avg = accumulate(times.begin(), times.end(), 0.0) / double(times.size());
    ofstream out(opt.results_csv, ios::app);
    string status = "ok";
    if (!status_extra.empty()) status += ";" + status_extra;
    status += ";ann_seed=" + to_string(opt.ann_seed);
    status += marsDiagnosticsStatus(query_stats);
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

static void runMarsTaskSet(const Options& opt, Dataset& ds, MarsIndex& idx, bool include_ablations) {
    if (include_ablations) {
        printTaskStart("MARS-NoSemanticGroupPruning", opt);
        auto no_prune_stats = marsIndexedStats(idx, MarsVariant::NoSemanticGroupPruning);
        runMarsQueries(opt, ds, "MARS-NoSemanticGroupPruning", [&](const vector<float>& q, int s) {
            return marsCompileThenSearchDetailed(ds, idx, opt, q, s);
        }, idx.mars_build_ms, idx.mars_index_mb, no_prune_stats.first, no_prune_stats.second, marsIndexStatus(idx));

        printTaskStart("MARS-OnlyQuantizedANN", opt);
        auto ann_stats = marsIndexedStats(idx, MarsVariant::OnlyQuantizedANN);
        runMarsQueries(opt, ds, "MARS-OnlyQuantizedANN", [&](const vector<float>& q, int s) {
            return marsCompileThenSearchDetailed(ds, idx, opt, q, s);
        }, idx.quantized_ann_build_ms, idx.quantized_ann_index_mb, ann_stats.first, ann_stats.second, marsIndexStatus(idx));

        printTaskStart("MARS-OnlySymphonyQG", opt);
        auto sym_stats = marsIndexedStats(idx, MarsVariant::OnlySymphonyQG);
        runMarsQueries(opt, ds, "MARS-OnlySymphonyQG", [&](const vector<float>& q, int s) {
            return marsCompileThenSearchDetailed(ds, idx, opt, q, s);
        }, idx.symphony_only_build_ms, idx.symphony_only_index_mb, sym_stats.first, sym_stats.second, marsIndexStatus(idx));
    }

    printTaskStart("MARS", opt);
    auto full_stats = marsIndexedStats(idx, MarsVariant::Full);
    runMarsQueries(opt, ds, "MARS", [&](const vector<float>& q, int s) {
        return marsCompileThenSearchDetailed(ds, idx, opt, q, s);
    }, idx.mars_build_ms, idx.mars_index_mb, full_stats.first, full_stats.second, marsIndexStatus(idx));
}

int main(int argc, char** argv) {
    if (argc > 1 && string(argv[1]) == "--tasks") {
        string task_file;
        Options base = parseBatchArgs(argc, argv, task_file);
        vector<ExperimentTask> tasks = readTaskFile(task_file);
        if (tasks.empty()) throw runtime_error("Empty task file: " + task_file);
        base.build_ablation_indexes = any_of(tasks.begin(), tasks.end(), [](const ExperimentTask& task) {
            return task.param_name == "ablation";
        });
        Options first = optionsForTask(base, tasks[0]);
        Dataset ds = loadDataset(first);
        MarsIndex idx = buildMarsIndex(ds, base);
        size_t task_i = 0;
        for (const auto& task : tasks) {
            Options opt = optionsForTask(base, task);
            loadTaskData(ds, opt);
            cout << "[RUN] " << opt.region << " MARS task " << (++task_i) << "/" << tasks.size()
                 << " param=" << opt.param_name << "=" << opt.param_value
                 << " query_tag=" << opt.query_tag
                 << " tau=" << int(opt.tau)
                 << " k=" << opt.k << endl;
            bool include_ablations = (opt.param_name == "ablation");
            runMarsTaskSet(opt, ds, idx, include_ablations);
        }
        delete ds.poi_reader;
        delete ds.cat_reader;
        return 0;
    }

    Options opt = parseArgs(argc, argv);
    opt.build_ablation_indexes = true;
    Dataset ds = loadDataset(opt);
    MarsIndex idx = buildMarsIndex(ds, opt);
    runMarsTaskSet(opt, ds, idx, true);
    delete ds.poi_reader;
    delete ds.cat_reader;
    return 0;
}
