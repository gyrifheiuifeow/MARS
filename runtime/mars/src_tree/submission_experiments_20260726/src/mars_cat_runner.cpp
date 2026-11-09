#include "student_diagnostics.hpp"
#define h2hQuery studentH2HQuery
// MARS query backend with frequency-partitioned pivot and rest channels.
// Pivot vectors use DFS-key posting lists. Rest nodes represent (vector, object)
// associations and are searched with SymphonyQG before score aggregation.

#define main mars_exp_main_unused
#include "../../code/mars_experiment_runner.cpp"
#undef main

#include "qg_theta.hpp"  // rabitqlib::symqg_theta::QuantizedGraphTheta (local copy)
#include "channel_topk.hpp"
#include "qg_build_contract.hpp"

#include <chrono>
#include <cstdio>

enum class CatScoreMode {
    FullObjectScore,
    ChannelScore,
};

static const char* catScoreModeName(CatScoreMode mode) {
    return mode == CatScoreMode::ChannelScore ? "channel" : "full";
}

static vector<CatScoreMode> catRequestedScoreModes() {
    const char* value = getenv("MARS_CAT_SCORE_MODE");
    string mode = value ? value : "full";
    if (mode == "full") return {CatScoreMode::FullObjectScore};
    if (mode == "channel") return {CatScoreMode::ChannelScore};
    if (mode == "both") return {CatScoreMode::FullObjectScore, CatScoreMode::ChannelScore};
    if (mode == "both-reverse") return {CatScoreMode::ChannelScore, CatScoreMode::FullObjectScore};
    throw runtime_error("MARS_CAT_SCORE_MODE must be full, channel, both, or both-reverse");
}

// Store fully reachable blocks as DFS-key intervals. Check fringe vertices
// with H2H when a candidate object is encountered.
static void catCompileRoadPredicateBlock(
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
    if (status == 0) return;  // outside
    if (status == 2) {        // fully inside -> trusted DFS-key interval
        pred.stats.inside_blocks += 1.0;
        pred.inside_blocks.push_back({level, block_id});
        addCompiledInterval(pred, block);
        return;
    }
    // partial
    pred.stats.partial_blocks += 1.0;
    if (level <= 1) {
        for (int v : block.vertices) {
            if (v <= 0 || v > ds.road_n) continue;
            if (v >= static_cast<int>(ds.objects_by_vertex.size()) || ds.objects_by_vertex[v].empty()) continue;
            pred.fringe_vertices.insert(v);
        }
        return;
    }
    for (int child_id : block.children) {
        catCompileRoadPredicateBlock(ds, hierarchy, level - 1, child_id, source_vertex, tau, pred);
    }
}

static CompiledRoadPredicate catCompileRoadPredicate(
    Dataset& ds,
    const RoadBlockHierarchy& hierarchy,
    int source_vertex,
    double tau
) {
    CompiledRoadPredicate pred;
    if (hierarchy.levels.empty()) return pred;
    int max_level = static_cast<int>(hierarchy.levels.size()) - 1;
    for (const RoadBlock& block : hierarchy.levels[max_level]) {
        catCompileRoadPredicateBlock(ds, hierarchy, max_level, block.id, source_vertex, tau, pred);
    }
    coalesceCompiledIntervals(pred);
    pred.stats.compiled_fringe_vertices = double(pred.fringe_vertices.size());
    return pred;
}

// Pivot vectors and their object posting lists, sorted by DFS key.
struct CatPostingEntry {
    int dfs_key;
    int object_id;
};

struct CatIndex {
    vector<int> slot_cid;
    vector<const float*> slot_vec;
    vector<vector<CatPostingEntry>> postings;  // [slot] -> sorted-by-dfs_key
    size_t total_posting_entries = 0;
    size_t dim = 0;
};

#include "freq_partition.hpp"

static CatIndex buildCatIndex(Dataset& ds, MarsIndex& idx) { return buildFrequencyPivotIndex(ds,idx); }

// Rest graph with one node per (merged vector, object) association.
// Build with QGBuilder and load the saved graph through QuantizedGraphTheta.
struct NameGraph {
    rabitqlib::symqg_theta::QuantizedGraphTheta<float> qg;
    vector<int> pid2oid;   // graph PID -> object_id
    vector<uint32_t> pid2uid; // graph PID -> merged vector UID
    size_t num = 0;
    size_t dim = 0;
    double build_ms = 0.0;
    double index_mb = 0.0;
    bool ready = false;
};

static size_t restAssociationCount(){size_t n=0;for(auto uid:frequency_partition.rest_uids)n+=frequency_partition.owners.at(uid).size();return n;}
static void populateRestMapping(NameGraph& ng){
 ng.pid2oid.clear();ng.pid2uid.clear();ng.pid2oid.reserve(restAssociationCount());ng.pid2uid.reserve(restAssociationCount());
 for(auto uid:frequency_partition.rest_uids){if(frequency_partition.channels.at(uid))throw runtime_error("pivot leaked into rest map");for(auto oid:frequency_partition.owners.at(uid)){ng.pid2uid.push_back(uid);ng.pid2oid.push_back(int(oid));}}
 ng.num=ng.pid2oid.size();
}
static void validateRestMapping(const NameGraph&ng){
 if(ng.num!=restAssociationCount()||ng.pid2uid.size()!=ng.num||ng.pid2oid.size()!=ng.num)throw runtime_error("rest association/node count mismatch");
 size_t pid=0;for(auto uid:frequency_partition.rest_uids)for(auto oid:frequency_partition.owners.at(uid)){if(ng.pid2uid.at(pid)!=uid||ng.pid2oid.at(pid)!=int(oid))throw runtime_error("rest node UID/object lost duplicated or reordered");++pid;}
}

static bool catUniqueEnabled() {
    const char* v = getenv("MARS_CAT_UNIQUE");
    if (!v) return true;            // default ON
    return atoi(v) != 0;
}

static bool catPivotEnabled() {
    const char* v = getenv("MARS_CAT_PIVOT");
    if (!v) return true;            // default ON
    return atoi(v) != 0;
}

static bool catPointwiseRoadEnabled() {
    const char* v = getenv("MARS_CAT_POINTWISE_ROAD");
    return v && atoi(v) != 0;
}

// Search-pool capacity for the rest graph.
static size_t catUniqueEf() {
    const char* v = getenv("MARS_CAT_UNIQUE_EF");
    if (v) { int e = atoi(v); if (e > 0) return static_cast<size_t>(e); }
    return 400;
}

static size_t catUniqueEfForTask(const Options& opt) {
    if (opt.param_name == "ef") {
        int value = atoi(opt.param_value.c_str());
        if (value > 0) return static_cast<size_t>(value);
    }
    return catUniqueEf();
}

// Build in place because NameGraph owns a non-copyable graph.
static void buildNameGraph(Dataset& ds, MarsIndex& idx, const Options& opt, NameGraph& ng) {
    ng.dim = idx.global.dim;
    const GlobalIndexData& g = idx.global;
    const size_t dim = g.dim;

    // Map each rest-vector association to a graph node.
    vector<float> name_matrix;populateRestMapping(ng);validateRestMapping(ng);
    name_matrix.reserve(ng.num*dim);
    for(uint32_t uid:ng.pid2uid){const float* v=frequency_partition.vectorAt(uid);name_matrix.insert(name_matrix.end(),v,v+dim);}
    if (ng.num <= static_cast<size_t>(opt.qg_degree)) {
        cout << "[BUILD] MARS-CAT name graph SKIPPED (too few name vectors: " << ng.num << ")" << endl;
        return;  // ready=false
    }

    cout << "[BUILD] MARS-CAT unique(name) channel: building SymphonyQG over "
         << ng.num << " name vectors (dim=" << dim << ", deg=" << opt.qg_degree
         << ", ef_build=" << opt.qg_ef_build << ")" << endl;
    auto t0 = chrono::high_resolution_clock::now();
    const char* student_graph=getenv("STUDENT_NAME_GRAPH_FILE");
    if(!student_graph || !*student_graph)throw runtime_error("student graph path required");
    if(ifstream(student_graph,ios::binary).good()) {
        ng.qg.load(student_graph);
        cout << "[STUDENT] Loaded existing identical name graph" << endl;
    } else {
        rabitqlib::symqg::QuantizedGraph<float> builder_qg(
            ng.num, dim,
            reviewer_contract::requireSIMDSafeQGDegree(opt.qg_degree),
            rabitqlib::METRIC_IP
        );
        auto builder = reviewer_contract::makeSingleThreadQGBuilder(
            builder_qg, opt.qg_ef_build, name_matrix.data()
        );
        builder.build();
        // Save the built graph for loading through QuantizedGraphTheta.
        string path=student_graph;
        entry::phase(3);
        builder_qg.save(path.c_str());
        entry::phase(2);
        if(!getenv("FREQ_BUILD_ONLY"))ng.qg.load(path.c_str());
    }
    ng.qg.set_ef(catUniqueEf());
    auto t1 = chrono::high_resolution_clock::now();
    ng.build_ms = chrono::duration<double, milli>(t1 - t0).count();
    ng.index_mb = double(name_matrix.size() * sizeof(float)) / 1000000.0;
    ng.ready = true;
    cout << "[BUILD] MARS-CAT name graph ready: num=" << ng.num
         << " build_ms=" << ng.build_ms << " ef=" << catUniqueEf() << endl;
}

#ifndef STUDENT_FREQ_ACCEPTANCE
#define STUDENT_FREQ_ACCEPTANCE 0
#endif
#if STUDENT_FREQ_ACCEPTANCE
struct FrequencyObservedUpdate {int oid;double score;int channel;};
static vector<FrequencyObservedUpdate> freq_observed_updates;
static vector<double> freq_returned_scores;
static bool freq_fixture_rest_enumeration=false;
#endif
// Query the pivot postings and rest graph, then aggregate object scores.
static MarsSearchResult marsCatSearch(
    Dataset& ds,
    MarsIndex& idx,
    CatIndex& ci,
    NameGraph& ng,
    const Options& opt,
    const vector<float>& q,
    int source_vertex,
    CatScoreMode score_mode
) {
#if STUDENT_FREQ_ACCEPTANCE
    freq_observed_updates.clear();freq_returned_scores.clear();
#endif
    const bool pointwise_road = catPointwiseRoadEnabled();
    CompiledRoadPredicate pred;
    studentSwitchPhase(1); // road compilation
    if (!pointwise_road) {
#if STUDENT_FREQ_ACCEPTANCE
        if(freq_fixture_rest_enumeration){pred.intervals.push_back({0,0});}
        else
#endif
        pred = catCompileRoadPredicate(ds, idx.hierarchy, source_vertex, opt.tau);
    }

    studentSwitchPhase(0); // normal per-query state initialization
    MarsQueryStats stats = pred.stats;

    QueryState state;
    ChannelTopK channel_topk(opt.k);
    unordered_set<int> pivot_seen;
    unordered_set<int> rest_seen;
    unordered_map<int, bool> fringe_feasibility;
    double rerank_dot_products = 0.0;
    double channel_score_updates = 0.0;

    auto currentTheta = [&]() {
        return score_mode == CatScoreMode::ChannelScore
            ? channel_topk.theta()
            : heapTheta(state, opt.k);
    };

    auto objectFeasible = [&](int oid) {
        if (oid < 0 || oid >= static_cast<int>(ds.objects.size())) return false;
        int vtx = ds.objects[oid].vertex_id;
        if (vtx <= 0 || vtx >= static_cast<int>(idx.hierarchy.vertex_key.size())) return false;
        if (pointwise_road) {
#if STUDENT_FREQ_ACCEPTANCE
            if(freq_fixture_rest_enumeration)return vtx==1;
#endif
            auto cached = fringe_feasibility.find(vtx);
            if (cached != fringe_feasibility.end()) return cached->second;
            stats.feasible_pointwise_h2h_calls += 1.0;
            double distance = h2hQuery(source_vertex - 1, vtx - 1);
            bool feasible = distance >= 0 && distance <= opt.tau;
            fringe_feasibility.emplace(vtx, feasible);
            return feasible;
        }
        int key = idx.hierarchy.vertex_key[vtx];
        if (pred.containsKey(key)) return true;
        if (pred.fringe_vertices.count(vtx) == 0) return false;
        auto cached = fringe_feasibility.find(vtx);
        if (cached != fringe_feasibility.end()) return cached->second;
        stats.feasible_pointwise_h2h_calls += 1.0;
        double distance = h2hQuery(source_vertex - 1, vtx - 1);
        bool feasible = distance >= 0 && distance <= opt.tau;
        fringe_feasibility.emplace(vtx, feasible);
        return feasible;
    };

    auto processFullObject = [&](int oid) {
        if (state.evaluated_objects.count(oid)) return;
        rerank_dot_products += 1.0 + double(ds.objects[oid].category_ids.size());
        pushEvaluatedObject(state, ds, idx.global, q, oid, opt.k, &stats);
    };

    auto processPivotObject = [&](int oid, double rel_cid) {
        if (score_mode == CatScoreMode::FullObjectScore) {
            processFullObject(oid);
            return;
        }
        if (!pivot_seen.insert(oid).second) return;
#if STUDENT_FREQ_ACCEPTANCE
        freq_observed_updates.push_back({oid,rel_cid,0});
#endif
        if (channel_topk.update(oid, rel_cid)) channel_score_updates += 1.0;
    };

    auto processRestObject = [&](int oid, double similarity) {
        if (score_mode == CatScoreMode::FullObjectScore) {
            processFullObject(oid);
            return;
        }
        if (!rest_seen.insert(oid).second) return;
#if STUDENT_FREQ_ACCEPTANCE
        freq_observed_updates.push_back({oid,similarity,1});
#endif
        if (channel_topk.update(oid, similarity)) channel_score_updates += 1.0;
    };

    studentSwitchPhase(2); // pivot channel
    studentPivotPart(0);
    double expanded_categories = 0.0;
    if (catPivotEnabled()) {
        // Visit pivot posting lists in descending query similarity.
        const size_t C = ci.slot_cid.size();
        const size_t dim = ci.dim;
        vector<pair<double, int>> rel;
        rel.reserve(C);
        for (size_t s = 0; s < C; ++s) {
            const float* v = ci.slot_vec[s];
            double dot = 0.0;
            for (size_t d = 0; d < dim; ++d) dot += double(q[d]) * double(v[d]);
            rel.push_back({dot, int(s)});
        }
        studentPivotPart(1);
        sort(rel.begin(), rel.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        studentPivotPart(2);

        // Stop scanning pivot vectors once their similarity falls below theta.
        for (const auto& r : rel) {
            double rel_cid = r.first;
            double theta = currentTheta();
            if (rel_cid < theta) break;  // remaining pivot similarities are below theta
            int slot = r.second;
            const vector<CatPostingEntry>& posting = ci.postings[slot];
            if (posting.empty()) continue;
            expanded_categories += 1.0;
            stats.feasible_category_checks += 1.0;

            if (pointwise_road) {
                for (const auto& entry : posting) {
                    studentCountPost(0);
                    int oid = entry.object_id;
                    if (score_mode == CatScoreMode::FullObjectScore && state.evaluated_objects.count(oid)) continue;
                    if (score_mode == CatScoreMode::ChannelScore && pivot_seen.count(oid)) continue;
                    if (!objectFeasible(oid)) continue;
                    processPivotObject(oid, rel_cid);
                }
            } else {
                for (const auto& iv : pred.intervals) {
                    int lo = iv.first, hi = iv.second;
                    size_t a = 0, b = posting.size();
                    while (a < b) {
                        studentCountBinary();
                        size_t mid = (a + b) / 2;
                        if (posting[mid].dfs_key < lo) a = mid + 1; else b = mid;
                    }
                    for (size_t j = a; j < posting.size() && posting[j].dfs_key <= hi; ++j) {
                        studentCountPost(1);
                        int oid = posting[j].object_id;
                        processPivotObject(oid, rel_cid);
                    }
                }
                for (int vtx : pred.fringe_vertices) {
                    if (vtx <= 0 || vtx >= static_cast<int>(idx.hierarchy.vertex_key.size())) continue;
                    int key = idx.hierarchy.vertex_key[vtx];
                    auto it = lower_bound(
                        posting.begin(), posting.end(), key,
                        [](const CatPostingEntry& entry, int target) {
                            studentCountBinary();
                            return entry.dfs_key < target;
                        }
                    );
                    for (; it != posting.end() && it->dfs_key == key; ++it) {
                        studentCountPost(2);
                        int oid = it->object_id;
                        if (score_mode == CatScoreMode::FullObjectScore && state.evaluated_objects.count(oid)) continue;
                        if (score_mode == CatScoreMode::ChannelScore && pivot_seen.count(oid)) continue;
                        if (!objectFeasible(oid)) continue;
                        processPivotObject(oid, rel_cid);
                    }
                }
            }
        }
    }
    studentPivotPart(3);
    stats.feasible_category_expanded_objects = expanded_categories;

    // Search the rest graph to beam convergence and filter visited nodes by theta.
    studentSwitchPhase(3); // rest channel
    bool unique_on = catUniqueEnabled() && ng.ready;
    if (unique_on) {
        float theta = static_cast<float>(currentTheta());
        ng.qg.set_ef(catUniqueEfForTask(opt));
        // Map the graph node to its object before checking road feasibility.
        auto accept = [&](rabitqlib::PID pid) -> bool {
            if (pid >= ng.pid2oid.size()) return false;
            int oid = ng.pid2oid[pid];
            if (oid < 0 || oid >= static_cast<int>(ds.objects.size())) return false;
            return objectFeasible(oid);
        };
        std::vector<rabitqlib::symqg_theta::ThetaHit<float>> hits;
        size_t steps = 0;
#if STUDENT_FREQ_ACCEPTANCE
        if(freq_fixture_rest_enumeration){
            for(size_t pid=0;pid<ng.pid2uid.size();++pid){
                const float* v=frequency_partition.vectorAt(ng.pid2uid[pid]);
                double sum=0;for(size_t d=0;d<q.size();++d)sum+=double(q[d])*double(v[d]);
                float sim=float(sum);++steps;
                if(sim>=theta&&accept(pid)){rabitqlib::symqg_theta::ThetaHit<float> h;h.id=pid;h.similarity=sim;hits.push_back(h);}
            }
        }else
#endif
        ng.qg.search_converge(q.data(), theta, accept, hits, steps);
        stats.route_graph = double(steps);                 // rest-graph beam steps
        stats.feasible_object_checks = double(hits.size()); // road-feasible rest nodes above theta
        // Process rest hits by descending similarity until the updated theta is reached.
        sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.similarity > b.similarity; });
        for (const auto& h : hits) {
            double theta_now = currentTheta();
            if (double(h.similarity) < theta_now) break;
            int oid = ng.pid2oid[h.id];
            processRestObject(oid, double(h.similarity));
        }
    }

    studentSwitchPhase(4); // normal returned-ID materialization
    vector<int> ids = score_mode == CatScoreMode::ChannelScore
        ? channel_topk.ids()
        : heapToObjectIds(state.heap);
#if STUDENT_DIAGNOSTICS
    stats.scanned_aspects = rerank_dot_products;
    stats.indexed_candidates = channel_score_updates;
    stats.exact_evaluated_objects = score_mode == CatScoreMode::ChannelScore
        ? double(channel_topk.observedObjectCount()) : double(state.evaluated_objects.size());
    stats.final_heap_size = double(ids.size());
#endif
#if STUDENT_FREQ_ACCEPTANCE
    for(int oid:ids)freq_returned_scores.push_back(channel_topk.score(oid));
#endif
    studentSwitchPhase(5); // return/copy and normal local-state destruction
    return {ids, stats};
}

static MarsIndex buildMarsCatIndex(Dataset& ds, const Options& opt) {
    cout << "[BUILD] " << opt.region << " MARS-CAT building H2H road index..." << endl;
    buildH2HIndex(opt.region);

    MarsIndex idx;
    cout << "[BUILD] " << opt.region << " MARS-CAT loading global aspect float matrix..." << endl;
    auto t0 = chrono::high_resolution_clock::now();
    idx.global = buildGlobalIndexData(ds, opt.max_index_vectors);
    auto t1 = chrono::high_resolution_clock::now();
    idx.global_load_ms = chrono::duration<double, milli>(t1 - t0).count();

    cout << "[BUILD] " << opt.region << " MARS-CAT building road hierarchy..." << endl;
    auto h0 = chrono::high_resolution_clock::now();
    idx.hierarchy = buildRoadBlockHierarchy(ds);
    auto h1 = chrono::high_resolution_clock::now();
    idx.hierarchy_build_ms = chrono::duration<double, milli>(h1 - h0).count();

    idx.mars_build_ms = idx.global_load_ms + idx.hierarchy_build_ms;
    idx.mars_index_mb = double(idx.global.matrix.size() * sizeof(float)) / 1000000.0;
    cout << "MARS-CAT index summary: aspects=" << idx.global.entries.size()
         << " dim=" << idx.global.dim
         << " categories=" << idx.global.cat_aspect_by_id.size()
         << " build_ms=" << idx.mars_build_ms << endl;
    return idx;
}

static string catIndexStatus(
    const CatIndex& ci,
    const NameGraph& ng,
    CatScoreMode score_mode,
    const Options& opt
) {
    ostringstream st;
    st << "cat_backend=dual_pivot_category+unique_name"
       << ";cat_categories=" << ci.slot_cid.size()
       << ";cat_posting_entries=" << ci.total_posting_entries
       << ";cat_score_mode=" << catScoreModeName(score_mode)
       << ";cat_theta_object_unique=1"
       << ";diag_scanned_aspects_metric=rerank_dot_products"
       << ";diag_indexed_candidates_metric=channel_score_updates"
       << ";cat_no_budget=1"
       << ";road_filter=" << (catPointwiseRoadEnabled() ? "pointwise_h2h" : "compiled_intervals")
       << ";pivot_channel=" << (catPivotEnabled() ? 1 : 0)
       << ";unique_name_channel=" << (catUniqueEnabled() && ng.ready ? 1 : 0)
       << ";unique_name_graph_ready=" << (ng.ready ? 1 : 0)
       << ";unique_name_vectors=" << ng.num
       << ";unique_name_ef=" << catUniqueEfForTask(opt);
    return st.str();
}

static void runCatTaskSet(const Options& opt, Dataset& ds, MarsIndex& idx, CatIndex& ci, NameGraph& ng) {
    bool pivot_on = catPivotEnabled();
    bool unique_on = catUniqueEnabled() && ng.ready;
    if (!pivot_on && !unique_on) throw runtime_error("Both MARS-CAT channels are disabled");
    string channel_name = pivot_on && unique_on ? "dual"
        : (pivot_on ? "pivot" : "rest");
    for (CatScoreMode score_mode : catRequestedScoreModes()) {
        string algo = "MARS-CAT-" + channel_name + "-" + catScoreModeName(score_mode);
        printTaskStart(algo, opt);
        runMarsQueries(opt, ds, algo, [&](const vector<float>& q, int s) {
            return marsCatSearch(ds, idx, ci, ng, opt, q, s, score_mode);
        }, idx.mars_build_ms, idx.mars_index_mb, ci.slot_cid.size(), idx.global.entries.size(), catIndexStatus(ci, ng, score_mode, opt));
    }
}

#ifndef STUDENT_EMBEDDED_MAIN
int main(int argc, char** argv) {
    try {
        if (argc > 1 && string(argv[1]) == "--tasks") {
            string task_file;
            Options base = parseBatchArgs(argc, argv, task_file);
            vector<ExperimentTask> tasks = readTaskFile(task_file);
            if (tasks.empty()) throw runtime_error("Empty task file: " + task_file);
            Options first = optionsForTask(base, tasks[0]);
            Dataset ds = loadDataset(first);
            MarsIndex idx = buildMarsCatIndex(ds, base);
            CatIndex ci = buildCatIndex(ds, idx);
            cout << "[BUILD] MARS-CAT category index: categories=" << ci.slot_cid.size()
                 << " posting_entries=" << ci.total_posting_entries << endl;
            NameGraph ng;
            if (catUniqueEnabled()) buildNameGraph(ds, idx, base, ng);
            size_t task_i = 0;
            for (const auto& task : tasks) {
                Options opt = optionsForTask(base, task);
                loadTaskData(ds, opt);
                cout << "[RUN] " << opt.region << " MARS-CAT task " << (++task_i) << "/" << tasks.size()
                     << " tau=" << int(opt.tau) << " k=" << opt.k
                     << " pivot=" << (catPivotEnabled() ? 1 : 0)
                     << " unique=" << (catUniqueEnabled() && ng.ready ? 1 : 0) << endl;
                runCatTaskSet(opt, ds, idx, ci, ng);
            }
            delete ds.poi_reader;
            delete ds.cat_reader;
            return 0;
        }

        Options opt = parseArgs(argc, argv);
        Dataset ds = loadDataset(opt);
        MarsIndex idx = buildMarsCatIndex(ds, opt);
        CatIndex ci = buildCatIndex(ds, idx);
        NameGraph ng;
        if (catUniqueEnabled()) buildNameGraph(ds, idx, opt, ng);
        runCatTaskSet(opt, ds, idx, ci, ng);
        delete ds.poi_reader;
        delete ds.cat_reader;
        return 0;
    } catch (const exception& e) {
        cerr << "MARS-CAT error: " << e.what() << endl;
        return 2;
    }
}

#endif // STUDENT_EMBEDDED_MAIN
