#include "../../baselines/mars_baselines/baseline_common.hpp"

#include "mesh_road_core.hpp"
#include "mesh_road_hnsw.hpp"
#include "mesh_road_placement.hpp"
#include "mesh_runner_contract.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <set>

namespace {

constexpr double kPi = 3.14159265358979323846;

struct VertexCoordLite {
    double x = 0.0;
    double y = 0.0;
    bool present = false;
};

struct NodeIndexData {
    vector<float> matrix;
    vector<meshroad::LocalAspect> aspects;
    vector<vector<int>> postings;
    size_t dim = 0;
};

struct MeshRoadRuntime {
    meshroad::STRTree tree;
    vector<VertexCoordLite> coords;
    double alpha = 0.0;
    unordered_set<int> selected_nodes;
    unordered_map<int, unique_ptr<meshroad::LocalHNSW>> local_indexes;
    uint64_t serialized_bytes = 0;
    size_t indexed_aspects = 0;
    double build_ms = 0.0;
    string placement_manifest;
    double placement_initial_work = 0.0;
    double placement_final_work = 0.0;
    uint64_t placement_estimated_bytes = 0;
    double calibration_h2h_ms = 0.0;
    double calibration_score_ms = 0.0;
    double calibration_ann_ms = 0.0;
    size_t calibration_ann_aspects = 0;
    bool frozen_placement = false;
};

struct WorkCalibration {
    double h2h_ms_per_object = 0.0;
    double score_ms_per_object = 0.0;
    double ann_ms_per_node = 0.0;
    size_t ann_reference_aspects = 0;
};

struct MeshSearchResult {
    vector<int> ids;
    int attempts = 0;
    int final_budget = 0;
    int returned_size = 0;
    int h2h_calls = 0;
    int scored_objects = 0;
    int execution_nodes = 0;
    int hnsw_nodes = 0;
    int exact_leaf_nodes = 0;
    double internal_ms = 0.0;
};

static vector<VertexCoordLite> loadVertexCoordsLite(const string& region, int road_n) {
    const string path = dataPath(region, "USA-road-d." + region + ".co");
    ifstream in(path);
    if (!in.is_open()) throw runtime_error("Cannot open coordinate file: " + path);
    vector<array<double, 3>> raw;
    double ref_lat = 0.0;
    string line;
    while (getline(in, line)) {
        if (line.empty() || line[0] != 'v') continue;
        stringstream ss(line);
        char tag = 0;
        int id = 0;
        double lon_raw = 0.0, lat_raw = 0.0;
        ss >> tag >> id >> lon_raw >> lat_raw;
        if (id < 1 || id > road_n) continue;
        const double lon = lon_raw / 1000000.0;
        const double lat = lat_raw / 1000000.0;
        raw.push_back({double(id), lon, lat});
        ref_lat += lat;
    }
    if (raw.empty()) throw runtime_error("Coordinate file has no vertices: " + path);
    ref_lat /= double(raw.size());
    const double cos_lat = cos(ref_lat * kPi / 180.0);
    vector<VertexCoordLite> coords(road_n + 1);
    for (const auto& row : raw) {
        const int id = int(row[0]);
        coords[id].x = row[1] * 111320.0 * cos_lat;
        coords[id].y = row[2] * 110540.0;
        coords[id].present = true;
    }
    return coords;
}

static double certifiedAlphaFromLoadedGraph(
    const vector<VertexCoordLite>& coords,
    int road_n
) {
    double minimum = numeric_limits<double>::infinity();
    for (int u = 0; u < road_n; ++u) {
        const int uid = u + 1;
        if (!coords[uid].present) continue;
        for (const ID& edge : G[u]) {
            const int vid = edge.first + 1;
            if (vid < 1 || vid > road_n || !coords[vid].present) continue;
            const double length = hypot(
                coords[uid].x - coords[vid].x,
                coords[uid].y - coords[vid].y
            );
            if (length == 0.0) continue;
            if (!isfinite(edge.second) || edge.second <= 0.0) {
                throw runtime_error("Road graph has a non-positive or non-finite edge weight");
            }
            minimum = min(minimum, edge.second / length);
        }
    }
    if (!isfinite(minimum) || minimum <= 0.0) {
        throw runtime_error("Cannot derive certified geometric-cover alpha");
    }
    return nextafter(minimum, 0.0);
}

static vector<meshroad::MeshPoint> buildMeshPoints(
    const Dataset& ds,
    const vector<VertexCoordLite>& coords,
    size_t max_objects
) {
    const size_t count = max_objects == 0 ? ds.objects.size() : min(max_objects, ds.objects.size());
    vector<meshroad::MeshPoint> points;
    points.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto& object = ds.objects[i];
        if (object.vertex_id < 1 || object.vertex_id >= static_cast<int>(coords.size()) ||
            !coords[object.vertex_id].present) {
            throw runtime_error("Object has no usable coordinate: " + to_string(object.object_id));
        }
        points.push_back({
            object.object_id,
            object.vertex_id,
            coords[object.vertex_id].x,
            coords[object.vertex_id].y,
        });
    }
    return points;
}

static size_t localAspectCount(const Dataset& ds, const vector<int>& object_ids) {
    unordered_set<int> categories;
    for (int object_id : object_ids) {
        for (int category_id : ds.objects[object_id].category_ids) categories.insert(category_id);
    }
    return object_ids.size() + categories.size();
}

static NodeIndexData buildNodeIndexData(Dataset& ds, const vector<int>& object_ids) {
    NodeIndexData data;
    data.dim = ds.query_vecs.empty() ? 0 : ds.query_vecs.front().size();
    if (data.dim == 0) throw runtime_error("Cannot build Mesh local index without query dimension");
    map<int, vector<int>> category_objects;
    for (int object_id : object_ids) {
        const auto& object = ds.objects.at(object_id);
        const auto vector = ds.poi_reader->readNorm1Based(object.poi_id);
        if (vector.size() != data.dim) throw runtime_error("POI vector dimension mismatch");
        data.matrix.insert(data.matrix.end(), vector.begin(), vector.end());
        data.aspects.push_back({object_id, 0, false});
        data.postings.push_back({object_id});
        for (int category_id : object.category_ids) category_objects[category_id].push_back(object_id);
    }
    for (auto& item : category_objects) {
        auto& posting = item.second;
        sort(posting.begin(), posting.end());
        posting.erase(unique(posting.begin(), posting.end()), posting.end());
        const auto vector = ds.cat_reader->readNorm1Based(item.first);
        if (vector.size() != data.dim) throw runtime_error("Category vector dimension mismatch");
        data.matrix.insert(data.matrix.end(), vector.begin(), vector.end());
        data.aspects.push_back({-1, item.first, true});
        data.postings.push_back(posting);
    }
    return data;
}

static double localPostingExpansion(const Dataset& ds, const vector<int>& object_ids) {
    unordered_set<int> categories;
    size_t category_occurrences = 0;
    for (int object_id : object_ids) {
        const auto& object = ds.objects.at(object_id);
        category_occurrences += object.category_ids.size();
        for (int category_id : object.category_ids) categories.insert(category_id);
    }
    const size_t aspects = object_ids.size() + categories.size();
    if (aspects == 0) return 1.0;
    return double(object_ids.size() + category_occurrences) / double(aspects);
}

static uint64_t estimateNodeBytes(size_t aspects, size_t dim, int hnsw_m) {
    const uint64_t vector_bytes = uint64_t(aspects) * uint64_t(dim) * sizeof(float);
    const uint64_t graph_bytes = uint64_t(aspects) * uint64_t(max(2, hnsw_m * 2)) * sizeof(uint32_t);
    const uint64_t metadata_bytes = uint64_t(aspects) * 32ULL;
    const long double raw = static_cast<long double>(
        vector_bytes + graph_bytes + metadata_bytes
    );
    return static_cast<uint64_t>(ceil(raw * 1.35L));
}

static WorkCalibration calibrateConstructionWork(
    Dataset& ds,
    const Options& opt,
    const meshroad::STRTree& tree,
    const vector<QueryMeta>& training,
    const vector<vector<float>>& training_vectors
) {
    if (training.empty() || training.size() != training_vectors.size()) {
        throw runtime_error("Mesh v2 construction metadata/vector counts do not match");
    }
    vector<int> leaf_ids;
    for (const auto& node : tree.nodes()) if (node.leaf) leaf_ids.push_back(node.id);
    if (leaf_ids.empty()) throw runtime_error("Mesh v2 STR tree contains no leaves");
    sort(leaf_ids.begin(), leaf_ids.end(), [&](int lhs, int rhs) {
        const auto lsize = tree.objects(lhs).size();
        const auto rsize = tree.objects(rhs).size();
        if (lsize != rsize) return lsize < rsize;
        return lhs < rhs;
    });
    const int sample_node = leaf_ids[leaf_ids.size() / 2];
    const auto& sample_objects = tree.objects(sample_node);
    if (sample_objects.empty()) throw runtime_error("Mesh v2 calibration leaf is empty");

    const size_t query_samples = min<size_t>(32, training.size());
    const size_t object_samples = min<size_t>(64, sample_objects.size());
    volatile double calibration_sink = 0.0;
    size_t calls = 0;
    auto h2h_begin = chrono::steady_clock::now();
    for (size_t qi = 0; qi < query_samples; ++qi) {
        for (size_t oi = 0; oi < object_samples; ++oi) {
            const int object_id = sample_objects[(oi * sample_objects.size()) / object_samples];
            calibration_sink += h2hQuery(
                training[qi].source_vertex_id - 1,
                ds.objects[object_id].vertex_id - 1
            );
            calls++;
        }
    }
    const double h2h_ms = chrono::duration<double, milli>(
        chrono::steady_clock::now() - h2h_begin
    ).count();

    auto score_begin = chrono::steady_clock::now();
    size_t score_calls = 0;
    for (size_t qi = 0; qi < query_samples; ++qi) {
        for (size_t oi = 0; oi < object_samples; ++oi) {
            const int object_id = sample_objects[(oi * sample_objects.size()) / object_samples];
            calibration_sink += objectScore(
                training_vectors[qi], ds.objects[object_id], ds
            );
            score_calls++;
        }
    }
    const double score_ms = chrono::duration<double, milli>(
        chrono::steady_clock::now() - score_begin
    ).count();

    NodeIndexData sample = buildNodeIndexData(ds, sample_objects);
    meshroad::LocalHNSW sample_index;
    sample_index.build(
        sample.matrix,
        sample.aspects.size(),
        sample.dim,
        sample.aspects,
        sample.postings,
        opt.hnsw_m,
        opt.hnsw_ef_build,
        opt.mesh_seed + static_cast<uint64_t>(sample_node)
    );
    const size_t result_limit = min<size_t>(
        sample_index.size(), static_cast<size_t>(opt.mesh_candidate_budget)
    );
    auto ann_begin = chrono::steady_clock::now();
    for (size_t qi = 0; qi < query_samples; ++qi) {
        const auto aspects = sample_index.search(
            training_vectors[qi], opt.mesh_ef, result_limit
        );
        calibration_sink += double(aspects.size());
    }
    const double ann_ms = chrono::duration<double, milli>(
        chrono::steady_clock::now() - ann_begin
    ).count();
    if (!isfinite(calibration_sink)) {
        throw runtime_error("Mesh v2 calibration produced a non-finite sink");
    }

    WorkCalibration result;
    result.h2h_ms_per_object = max(1e-9, h2h_ms / double(max<size_t>(1, calls)));
    result.score_ms_per_object = max(1e-9, score_ms / double(max<size_t>(1, score_calls)));
    result.ann_ms_per_node = max(1e-9, ann_ms / double(query_samples));
    result.ann_reference_aspects = sample_index.size();
    return result;
}

static vector<meshroad::PlacementWorkloadPoint> buildPlacementWorkload(
    const vector<QueryMeta>& training,
    const vector<VertexCoordLite>& coords,
    double alpha,
    const vector<double>& taus
) {
    vector<meshroad::PlacementWorkloadPoint> workload;
    workload.reserve(training.size() * taus.size());
    for (const auto& query : training) {
        if (query.source_vertex_id < 1 ||
            query.source_vertex_id >= static_cast<int>(coords.size()) ||
            !coords[query.source_vertex_id].present) {
            throw runtime_error("Training query has no usable source coordinate");
        }
        const auto& source = coords[query.source_vertex_id];
        for (double tau : taus) {
            workload.push_back({
                source.x,
                source.y,
                meshroad::certifiedRadius(tau, alpha),
                1.0 / double(taus.size()),
            });
        }
    }
    return workload;
}

static uint64_t saveTreeMetadata(
    const meshroad::STRTree& tree,
    double alpha,
    const filesystem::path& path
) {
    ofstream out(path, ios::binary | ios::trunc);
    if (!out) throw runtime_error("Cannot write Mesh STR metadata: " + path.string());
    const uint64_t magic = 0x4d45534853545231ULL;
    const uint64_t count = tree.nodes().size();
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&alpha), sizeof(alpha));
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (const auto& node : tree.nodes()) {
        const int32_t id = node.id;
        const int32_t parent = node.parent;
        const uint8_t leaf = node.leaf ? 1 : 0;
        const uint64_t child_count = node.children.size();
        const uint64_t object_count = node.object_ids.size();
        out.write(reinterpret_cast<const char*>(&id), sizeof(id));
        out.write(reinterpret_cast<const char*>(&parent), sizeof(parent));
        out.write(reinterpret_cast<const char*>(&leaf), sizeof(leaf));
        out.write(reinterpret_cast<const char*>(&node.box), sizeof(node.box));
        out.write(reinterpret_cast<const char*>(&child_count), sizeof(child_count));
        for (int child : node.children) {
            const int32_t value = child;
            out.write(reinterpret_cast<const char*>(&value), sizeof(value));
        }
        out.write(reinterpret_cast<const char*>(&object_count), sizeof(object_count));
        for (int object_id : node.object_ids) {
            const int32_t value = object_id;
            out.write(reinterpret_cast<const char*>(&value), sizeof(value));
        }
    }
    out.close();
    if (!out) throw runtime_error("Failed writing Mesh STR metadata");
    return filesystem::file_size(path);
}

static MeshRoadRuntime buildMeshRoadRuntime(Dataset& ds, const Options& opt) {
    const auto build_begin = chrono::steady_clock::now();
    entry::phase(1);
    MeshRoadRuntime runtime;
    runtime.coords = loadVertexCoordsLite(opt.region, ds.road_n);
    runtime.alpha = certifiedAlphaFromLoadedGraph(runtime.coords, ds.road_n);
    const auto points = buildMeshPoints(ds, runtime.coords, opt.max_index_vectors);
    entry::phase(2);
    runtime.tree.build(points, static_cast<size_t>(opt.mesh_leaf_capacity));
    entry::phase(1);

    meshroad::validateTrainingWorkloadTag(opt.mesh_training_tag, opt.query_tag);
    meshroad::validateMeshSearchParameters(
        opt.mesh_ef, opt.mesh_candidate_budget, opt.mesh_max_candidates, opt.k
    );
    const auto placement_taus = meshroad::parseAndValidatePlacementTaus(
        opt.mesh_placement_taus
    );
    const auto training = readQueryMeta(
        ds.prefix + "queries/" + opt.mesh_training_tag + ".meta.tsv"
    );
    const auto training_vectors = readFvecs(
        ds.prefix + "queries/" + opt.mesh_training_tag + ".fvecs"
    );
    if (training.size() != 1500) {
        throw runtime_error("Mesh placement workload must contain exactly 1500 queries");
    }
    if (training_vectors.size() != training.size()) {
        throw runtime_error("Mesh placement workload vectors must contain exactly 1500 queries");
    }
    const auto calibration = calibrateConstructionWork(
        ds, opt, runtime.tree, training, training_vectors
    );
    runtime.calibration_h2h_ms = calibration.h2h_ms_per_object;
    runtime.calibration_score_ms = calibration.score_ms_per_object;
    runtime.calibration_ann_ms = calibration.ann_ms_per_node;
    runtime.calibration_ann_aspects = calibration.ann_reference_aspects;
    const auto workload = buildPlacementWorkload(
        training, runtime.coords, runtime.alpha, placement_taus
    );

    vector<meshroad::NodeWorkEstimate> estimates;
    const double object_work = calibration.h2h_ms_per_object +
        calibration.score_ms_per_object;
    for (const auto& node : runtime.tree.nodes()) {
        const size_t aspects = localAspectCount(ds, node.object_ids);
        const uint64_t bytes = estimateNodeBytes(aspects, ds.query_vecs.front().size(), opt.hnsw_m);
        const double posting_expansion = localPostingExpansion(ds, node.object_ids);
        const double returned_objects = min<double>(
            node.object_ids.size(),
            min<size_t>(aspects, static_cast<size_t>(opt.mesh_candidate_budget)) *
                posting_expansion
        );
        const double ann_scale = log2(double(aspects) + 1.0) /
            log2(double(calibration.ann_reference_aspects) + 1.0);
        const double indexed_work = calibration.ann_ms_per_node * ann_scale +
            returned_objects * object_work;
        estimates.push_back({
            node.id,
            bytes,
            double(node.object_ids.size()) * object_work,
            indexed_work,
        });
    }
    const uint64_t memory_budget = static_cast<uint64_t>(
        max(0.0, opt.mesh_memory_mb) * 1000000.0
    );

    filesystem::path index_dir = opt.mesh_index_dir.empty()
        ? filesystem::path(opt.results_csv).parent_path() / "mesh_indexes" / opt.region
        : filesystem::path(opt.mesh_index_dir) / opt.region;
    entry::phase(3);
    filesystem::create_directories(index_dir);
    runtime.serialized_bytes += saveTreeMetadata(
        runtime.tree, runtime.alpha, index_dir / "str.meta"
    );
    entry::phase(1);
    constexpr uint64_t manifest_reserve = 1000000ULL;
    if (memory_budget <= runtime.serialized_bytes + manifest_reserve) {
        throw runtime_error("Mesh v2 memory budget cannot hold STR metadata and manifest");
    }
    const uint64_t placement_budget =
        memory_budget - runtime.serialized_bytes - manifest_reserve;
    meshroad::PlacementSelection selection;
    if (opt.mesh_frozen_placement_manifest.empty()) {
        selection = meshroad::selectMarginalPlacements(
            runtime.tree, workload, estimates, placement_budget
        );
    } else {
        ifstream frozen(opt.mesh_frozen_placement_manifest);
        if (!frozen) {
            throw runtime_error(
                "Cannot read frozen Mesh v2 placement manifest: " +
                opt.mesh_frozen_placement_manifest
            );
        }
        const string manifest_text(
            (istreambuf_iterator<char>(frozen)), istreambuf_iterator<char>()
        );
        selection.node_ids = meshroad::parseFrozenSelectedNodes(manifest_text);
        const auto estimates_by_node = meshroad::validateWorkEstimates(
            runtime.tree, workload, estimates
        );
        unordered_set<int> frozen_nodes;
        for (int node_id : selection.node_ids) {
            if (node_id < 0 || node_id >= static_cast<int>(runtime.tree.nodes().size())) {
                throw runtime_error("Frozen Mesh v2 placement node is outside the STR tree");
            }
            selection.used_bytes += estimates_by_node.at(node_id).estimated_bytes;
            frozen_nodes.insert(node_id);
        }
        if (selection.used_bytes > placement_budget) {
            throw runtime_error("Frozen Mesh v2 placement exceeds the declared estimated budget");
        }
        unordered_set<int> empty_nodes;
        selection.initial_work = meshroad::predictedPlacementWork(
            runtime.tree, workload, estimates_by_node, empty_nodes
        );
        selection.final_work = meshroad::predictedPlacementWork(
            runtime.tree, workload, estimates_by_node, frozen_nodes
        );
        runtime.frozen_placement = true;
    }
    runtime.selected_nodes.insert(selection.node_ids.begin(), selection.node_ids.end());
    runtime.placement_initial_work = selection.initial_work;
    runtime.placement_final_work = selection.final_work;
    runtime.placement_estimated_bytes = selection.used_bytes;

    for (int node_id : selection.node_ids) {
        NodeIndexData local = buildNodeIndexData(ds, runtime.tree.objects(node_id));
        entry::phase(2);
        auto index = make_unique<meshroad::LocalHNSW>();
        index->build(
            local.matrix,
            local.aspects.size(),
            local.dim,
            local.aspects,
            local.postings,
            opt.hnsw_m,
            opt.hnsw_ef_build,
            opt.mesh_seed + static_cast<uint64_t>(node_id)
        );
        const filesystem::path prefix = index_dir / ("node_" + to_string(node_id));
        entry::phase(3);
        index->save(prefix);
        entry::account(prefix.string()+".hnsw",uint64_t(local.aspects.size())*local.dim*sizeof(float),"Mesh local graph; raw floating vectors excluded");
        entry::account(prefix.string()+".meta",0,"Mesh aspect IDs and postings");
        entry::phase(1);
        runtime.serialized_bytes += index->serializedBytes();
        runtime.indexed_aspects += index->size();
        runtime.local_indexes.emplace(node_id, std::move(index));
    }

    entry::phase(3);
    entry::account(index_dir/"str.meta",0,"Mesh STR structure");
    const filesystem::path manifest = index_dir / "placement_manifest.json";
    ofstream json(manifest, ios::trunc);
    if (!json) throw runtime_error("Cannot write Mesh placement manifest");
    json << "{\n"
         << "  \"schema_version\": \"mesh-road-v2\",\n"
         << "  \"algorithm\": \"Mesh-Road\",\n"
         << "  \"region\": \"" << opt.region << "\",\n"
         << "  \"training_tag\": \"" << opt.mesh_training_tag << "\",\n"
         << "  \"evaluation_tag\": \"" << opt.query_tag << "\",\n"
         << "  \"placement_taus\": [2000,4000,6000,8000,10000],\n"
         << "  \"alpha\": " << setprecision(17) << runtime.alpha << ",\n"
         << "  \"memory_budget_bytes\": " << memory_budget << ",\n"
         << "  \"estimated_selected_bytes\": " << selection.used_bytes << ",\n"
         << "  \"calibration_h2h_ms_per_object\": " << calibration.h2h_ms_per_object << ",\n"
         << "  \"calibration_score_ms_per_object\": " << calibration.score_ms_per_object << ",\n"
         << "  \"calibration_ann_ms_per_node\": " << calibration.ann_ms_per_node << ",\n"
         << "  \"calibration_ann_reference_aspects\": " << calibration.ann_reference_aspects << ",\n"
         << "  \"predicted_initial_work\": " << selection.initial_work << ",\n"
         << "  \"predicted_final_work\": " << selection.final_work << ",\n"
         << "  \"mesh_leaf_capacity\": " << opt.mesh_leaf_capacity << ",\n"
         << "  \"mesh_ef\": " << opt.mesh_ef << ",\n"
         << "  \"mesh_candidate_budget\": " << opt.mesh_candidate_budget << ",\n"
         << "  \"mesh_seed\": " << opt.mesh_seed << ",\n"
         << "  \"placement_source\": \""
         << (runtime.frozen_placement ? "frozen_construction_manifest" : "construction_calibration")
         << "\",\n"
         << "  \"selected_nodes\": [";
    for (size_t i = 0; i < selection.node_ids.size(); ++i) {
        if (i) json << ',';
        json << selection.node_ids[i];
    }
    json << "],\n"
         << "  \"indexed_aspects\": " << runtime.indexed_aspects << ",\n"
         << "  \"serialized_bytes_before_manifest\": " << runtime.serialized_bytes << "\n"
         << "}\n";
    json.close();
    runtime.serialized_bytes += filesystem::file_size(manifest);
    meshroad::validateSerializedBudget(runtime.serialized_bytes, memory_budget);
    entry::account(manifest,0,"Mesh frozen placement and configuration");
    runtime.placement_manifest = manifest.string();
    runtime.build_ms = chrono::duration<double, milli>(chrono::steady_clock::now() - build_begin).count();
    return runtime;
}

static MeshSearchResult searchMeshRoad(
    Dataset& ds,
    MeshRoadRuntime& runtime,
    const Options& opt,
    const vector<float>& query,
    int source_vertex
) {
    const auto internal_begin = chrono::steady_clock::now();
    if (source_vertex < 1 || source_vertex >= static_cast<int>(runtime.coords.size()) ||
        !runtime.coords[source_vertex].present) {
        throw runtime_error("Mesh query source has no coordinate");
    }
    const auto& source = runtime.coords[source_vertex];
    const double radius = meshroad::certifiedRadius(opt.tau, runtime.alpha);
    vector<int> execution_nodes = runtime.tree.executionCover(
        source.x, source.y, radius, runtime.selected_nodes
    );

    MeshSearchResult result;
    result.execution_nodes = static_cast<int>(execution_nodes.size());
    vector<int8_t> feasibility(ds.objects.size(), -1);
    unordered_set<int> scored;
    priority_queue<Candidate> heap;
    auto feasible = [&](int object_id) {
        int8_t& cached = feasibility.at(object_id);
        if (cached >= 0) return cached != 0;
        result.h2h_calls++;
        const auto& object = ds.objects[object_id];
        const double distance = h2hQuery(source_vertex - 1, object.vertex_id - 1);
        cached = distance >= 0.0 && distance <= opt.tau ? 1 : 0;
        return cached != 0;
    };
    auto score_object = [&](int object_id) {
        if (!scored.insert(object_id).second) return;
        if (!feasible(object_id)) return;
        result.scored_objects++;
        const auto& object = ds.objects[object_id];
        pushTop(heap, opt.k, {objectScore(query, object, ds), object.object_id, object.vertex_id});
    };

    for (int node_id : execution_nodes) {
        if (runtime.selected_nodes.count(node_id)) result.hnsw_nodes++;
        else result.exact_leaf_nodes++;
    }

    int budget = opt.mesh_candidate_budget;
    const int max_budget = opt.mesh_max_candidates;
    while (true) {
        result.attempts++;
        for (int node_id : execution_nodes) {
            const auto found = runtime.local_indexes.find(node_id);
            if (found == runtime.local_indexes.end()) {
                if (result.attempts == 1) {
                    for (int object_id : runtime.tree.objects(node_id)) score_object(object_id);
                }
                continue;
            }
            const size_t limit = min<size_t>(
                found->second->size(), static_cast<size_t>(budget)
            );
            const auto aspects = found->second->search(query, opt.mesh_ef, limit);
            for (int object_id : found->second->expandObjects(aspects)) score_object(object_id);
        }
        result.final_budget = budget;
        if (static_cast<int>(heap.size()) >= opt.k || budget >= max_budget) break;
        budget = min(max_budget, budget * 2);
    }
    result.ids = heapToObjectIds(heap);
    result.returned_size = static_cast<int>(result.ids.size());
    // The measurement entry checks results after stopping its query timer.
    result.internal_ms = chrono::duration<double, milli>(chrono::steady_clock::now() - internal_begin).count();
    return result;
}

#include "../../../reference/mesh_search_original.hpp"

static double average(const vector<double>& values) {
    return values.empty() ? 0.0 : accumulate(values.begin(), values.end(), 0.0) / double(values.size());
}

static void runMeshQueries(
    const Options& opt,
    Dataset& ds,
    MeshRoadRuntime& runtime
) {
    if (ds.query_meta.size() != ds.query_vecs.size() || ds.query_meta.empty()) {
        throw runtime_error("Mesh query metadata/vector counts do not match");
    }
    vector<int> query_ids;
    query_ids.reserve(ds.query_meta.size());
    for (const auto& query : ds.query_meta) query_ids.push_back(query.query_id);
    meshroad::validateGroundTruthCoverage(query_ids, ds.gt.result_count, opt.k);

    vector<double> times, internal_times, attempts, budgets, returned, h2h_calls;
    vector<double> scored, execution_nodes, hnsw_nodes, exact_leaf_nodes;
    double recall_score_sum = 0.0, recall_id_sum = 0.0;
    int underfilled = 0;
    for (size_t i = 0; i < ds.query_vecs.size(); ++i) {
        auto measured = meshroad::measureOuterMilliseconds([&]() {
            return searchMeshRoad(
                ds, runtime, opt, ds.query_vecs[i], ds.query_meta[i].source_vertex_id
            );
        });
        const auto& prediction = measured.first;
        times.push_back(measured.second);
        internal_times.push_back(prediction.internal_ms);
        attempts.push_back(prediction.attempts);
        budgets.push_back(prediction.final_budget);
        returned.push_back(prediction.returned_size);
        h2h_calls.push_back(prediction.h2h_calls);
        scored.push_back(prediction.scored_objects);
        execution_nodes.push_back(prediction.execution_nodes);
        hnsw_nodes.push_back(prediction.hnsw_nodes);
        exact_leaf_nodes.push_back(prediction.exact_leaf_nodes);
        if (prediction.returned_size < opt.k) underfilled++;
        recall_score_sum += recallAtScoreThreshold(
            prediction.ids, ds.query_vecs[i], ds, ds.query_meta[i].query_id, opt.k
        );
        recall_id_sum += recallAtIds(prediction.ids, ds, ds.query_meta[i].query_id, opt.k);
    }

    const double avg_ms = average(times);
    const double query_count = double(times.size());
    ostringstream status;
    status << fixed << setprecision(6)
           << "ok;adaptation=mesh_inspired_wa_rtree_hnsw_roadverify"
           << ";mesh_version=v2"
           << ";road_feasibility=exact_h2h"
           << ";semantic_topk=approximate"
           << ";timing_boundary=outer_search_call"
           << ";alpha=" << runtime.alpha
           << ";mesh_seed=" << opt.mesh_seed
           << ";mesh_leaf_capacity=" << opt.mesh_leaf_capacity
           << ";mesh_memory_mb=" << opt.mesh_memory_mb
           << ";mesh_ef=" << opt.mesh_ef
           << ";mesh_candidate_budget=" << opt.mesh_candidate_budget
           << ";mesh_max_candidates=" << opt.mesh_max_candidates
           << ";placement_taus=2000|4000|6000|8000|10000"
           << ";placement_source="
           << (runtime.frozen_placement ? "frozen_construction_manifest" : "construction_calibration")
           << ";placement_estimated_bytes=" << runtime.placement_estimated_bytes
           << ";placement_predicted_initial_work=" << runtime.placement_initial_work
           << ";placement_predicted_final_work=" << runtime.placement_final_work
           << ";calibration_h2h_ms_per_object=" << runtime.calibration_h2h_ms
           << ";calibration_score_ms_per_object=" << runtime.calibration_score_ms
           << ";calibration_ann_ms_per_node=" << runtime.calibration_ann_ms
           << ";avg_attempts=" << average(attempts)
           << ";avg_final_budget=" << average(budgets)
           << ";p95_final_budget=" << percentile(budgets, 0.95)
           << ";avg_returned_size=" << average(returned)
           << ";underfilled_rate=" << double(underfilled) / query_count
           << ";feasible_rate=1.000000"
           << ";avg_h2h_calls=" << average(h2h_calls)
           << ";avg_scored_objects=" << average(scored)
           << ";avg_execution_nodes=" << average(execution_nodes)
           << ";avg_hnsw_nodes=" << average(hnsw_nodes)
           << ";avg_exact_leaf_nodes=" << average(exact_leaf_nodes)
           << ";avg_internal_search_ms=" << average(internal_times);

    meshroad::MeshCsvRow row;
    row.experiment_id = "exp";
    row.dataset = opt.region;
    row.algorithm = "Mesh-Road";
    row.param_name = opt.param_name;
    row.param_value = opt.param_value;
    row.query_tag = opt.query_tag;
    row.k = opt.k;
    row.tau = int(opt.tau);
    row.quant_bits = 0;
    row.ef_search = opt.mesh_ef;
    row.build_time_ms = runtime.build_ms;
    row.index_size_mb = double(runtime.serialized_bytes) / 1000000.0;
    row.avg_query_ms = avg_ms;
    row.p50_ms = percentile(times, 0.5);
    row.p95_ms = percentile(times, 0.95);
    row.qps = avg_ms > 0.0 ? 1000.0 / avg_ms : 0.0;
    row.recall_score = recall_score_sum / query_count;
    row.recall_id = recall_id_sum / query_count;
    row.indexed_block_groups = runtime.selected_nodes.size();
    row.indexed_aspects = runtime.indexed_aspects;
    row.status = status.str();

    ofstream out(opt.results_csv, ios::app);
    if (!out) throw runtime_error("Cannot append Mesh result CSV: " + opt.results_csv);
    out << meshroad::serializeMeshCsvRow(row) << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || string(argv[1]) != "--tasks") {
            cerr << "Usage: " << argv[0]
                 << " --tasks <REGION> <task_tsv> <results_csv> [key=value...]\n";
            return 2;
        }
        string task_file;
        Options base = parseBatchArgs(argc, argv, task_file);
        const vector<ExperimentTask> tasks = readTaskFile(task_file);
        if (tasks.empty()) throw runtime_error("Empty task file: " + task_file);
        Options first = optionsForTask(base, tasks.front());
        const auto first_search = meshroad::meshSearchParametersForTask(
            first.mesh_ef, first.mesh_candidate_budget, first.mesh_max_candidates,
            tasks.front().param_name, tasks.front().param_value, first.k
        );
        first.mesh_ef = first_search.ef;
        first.mesh_candidate_budget = first_search.candidate_budget;
        first.mesh_max_candidates = first_search.max_candidates;
        Dataset ds = loadDataset(first);
        cout << "[BUILD] " << first.region << " Mesh-Road building H2H index..." << endl;
        buildH2HIndex(first.region);
        cout << "[BUILD] " << first.region << " Mesh-Road building STR/local HNSW indexes..." << endl;
        MeshRoadRuntime runtime = buildMeshRoadRuntime(ds, first);
        cout << "[BUILD] " << first.region << " Mesh-Road selected_nodes="
             << runtime.selected_nodes.size() << " indexed_aspects=" << runtime.indexed_aspects
             << " serialized_mb=" << double(runtime.serialized_bytes) / 1000000.0
             << " build_ms=" << runtime.build_ms << endl;

        size_t task_index = 0;
        for (const auto& task : tasks) {
            Options opt = optionsForTask(base, task);
            const auto task_search = meshroad::meshSearchParametersForTask(
                opt.mesh_ef, opt.mesh_candidate_budget, opt.mesh_max_candidates,
                task.param_name, task.param_value, opt.k
            );
            opt.mesh_ef = task_search.ef;
            opt.mesh_candidate_budget = task_search.candidate_budget;
            opt.mesh_max_candidates = task_search.max_candidates;
            if (opt.mesh_leaf_capacity != first.mesh_leaf_capacity ||
                opt.mesh_memory_mb != first.mesh_memory_mb ||
                !meshroad::batchSearchParametersCompatible(
                    first_search, task_search, runtime.frozen_placement
                ) ||
                opt.mesh_seed != first.mesh_seed ||
                opt.mesh_training_tag != first.mesh_training_tag ||
                opt.mesh_placement_taus != first.mesh_placement_taus ||
                opt.mesh_frozen_placement_manifest != first.mesh_frozen_placement_manifest) {
                throw runtime_error(
                    "Mesh v2 batch tasks must share one build configuration; "
                    "search variation requires a frozen placement manifest"
                );
            }
            loadTaskData(ds, opt);
            printTaskStart("Mesh-Road", opt, ++task_index, tasks.size());
            runMeshQueries(opt, ds, runtime);
        }
        delete ds.poi_reader;
        delete ds.cat_reader;
        return 0;
    } catch (const exception& error) {
        cerr << "Mesh-Road fatal: " << error.what() << endl;
        return 1;
    }
}
