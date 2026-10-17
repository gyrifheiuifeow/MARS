static MeshSearchResult reference_searchMeshRoad(
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
    meshroad::validatePrediction(
        result.ids,
        opt.k,
        ds.objects.size(),
        [&](int object_id) { return feasible(object_id); }
    );
    result.internal_ms = chrono::duration<double, milli>(chrono::steady_clock::now() - internal_begin).count();
    return result;
}
