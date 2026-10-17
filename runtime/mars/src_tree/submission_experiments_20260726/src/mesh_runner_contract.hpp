#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace meshroad {

template <class FeasibleFn>
inline void validatePrediction(
    const std::vector<int>& ids,
    int k,
    std::size_t object_count,
    FeasibleFn feasible
) {
    if (k <= 0) throw std::invalid_argument("k must be positive");
    if (ids.size() > static_cast<std::size_t>(k)) {
        throw std::invalid_argument("prediction contains more than k objects");
    }
    std::unordered_set<int> seen;
    for (int object_id : ids) {
        if (object_id < 0 || static_cast<std::size_t>(object_id) >= object_count) {
            throw std::invalid_argument("prediction contains an invalid object ID");
        }
        if (!seen.insert(object_id).second) {
            throw std::invalid_argument("prediction contains a duplicate object ID");
        }
        if (!feasible(object_id)) {
            throw std::invalid_argument("prediction contains a road-infeasible object");
        }
    }
}

inline void validateGroundTruthCoverage(
    const std::vector<int>& query_ids,
    const std::unordered_map<int, int>& result_counts,
    int k
) {
    if (k <= 0) throw std::invalid_argument("k must be positive");
    std::unordered_set<int> seen;
    for (int query_id : query_ids) {
        if (!seen.insert(query_id).second) {
            throw std::invalid_argument("query metadata contains a duplicate query ID");
        }
        const auto found = result_counts.find(query_id);
        if (found == result_counts.end()) {
            throw std::invalid_argument("ground truth is missing a query ID");
        }
        if (found->second <= 0 || found->second > k) {
            throw std::invalid_argument("ground-truth result count is outside 1..k");
        }
    }
}

inline std::vector<double> parseAndValidatePlacementTaus(const std::string& text) {
    std::vector<double> values;
    std::istringstream input(text);
    std::string token;
    while (std::getline(input, token, ',')) {
        if (token.empty()) throw std::invalid_argument("Mesh v2 placement tau is empty");
        std::size_t consumed = 0;
        const double value = std::stod(token, &consumed);
        if (consumed != token.size() || !std::isfinite(value)) {
            throw std::invalid_argument("Mesh v2 placement tau is invalid");
        }
        values.push_back(value);
    }
    const std::vector<double> required = {2000.0, 4000.0, 6000.0, 8000.0, 10000.0};
    if (values != required) {
        throw std::invalid_argument(
            "Mesh v2 placement taus must be exactly 2000,4000,6000,8000,10000"
        );
    }
    return values;
}

inline void validateMeshSearchParameters(
    int ef_search,
    int candidate_budget,
    int max_candidates,
    int k
) {
    if (k <= 0 || candidate_budget < k || max_candidates != candidate_budget ||
        ef_search < candidate_budget) {
        throw std::invalid_argument(
            "Mesh v2 requires ef >= candidate_budget == max_candidates >= k"
        );
    }
}

struct MeshSearchParameters {
    int ef = 0;
    int candidate_budget = 0;
    int max_candidates = 0;
};

inline MeshSearchParameters meshSearchParametersForTask(
    int ef,
    int candidate_budget,
    int max_candidates,
    const std::string& param_name,
    const std::string& param_value,
    int k
) {
    MeshSearchParameters result{ef, candidate_budget, max_candidates};
    if (param_name == "mesh_candidate_budget") {
        std::size_t consumed = 0;
        const int budget = std::stoi(param_value, &consumed);
        if (consumed != param_value.size()) {
            throw std::invalid_argument("Mesh candidate budget task value is invalid");
        }
        result = {budget, budget, budget};
    }
    validateMeshSearchParameters(
        result.ef, result.candidate_budget, result.max_candidates, k
    );
    return result;
}

inline bool batchSearchParametersCompatible(
    const MeshSearchParameters& first,
    const MeshSearchParameters& current,
    bool frozen_placement
) {
    return frozen_placement || (
        first.ef == current.ef &&
        first.candidate_budget == current.candidate_budget &&
        first.max_candidates == current.max_candidates
    );
}

inline void validateSerializedBudget(
    std::uint64_t serialized_bytes,
    std::uint64_t budget_bytes
) {
    if (serialized_bytes > budget_bytes) {
        throw std::invalid_argument("Mesh v2 serialized index exceeds declared memory budget");
    }
}

inline std::vector<int> parseFrozenSelectedNodes(const std::string& manifest_text) {
    if (manifest_text.find("\"schema_version\"") == std::string::npos ||
        manifest_text.find("mesh-road-v2") == std::string::npos) {
        throw std::invalid_argument("frozen placement manifest is not mesh-road-v2");
    }
    const std::string key = "\"selected_nodes\"";
    const std::size_t key_pos = manifest_text.find(key);
    const std::size_t open = key_pos == std::string::npos
        ? std::string::npos
        : manifest_text.find('[', key_pos + key.size());
    const std::size_t close = open == std::string::npos
        ? std::string::npos
        : manifest_text.find(']', open + 1);
    if (open == std::string::npos || close == std::string::npos) {
        throw std::invalid_argument("frozen placement manifest has no selected_nodes array");
    }
    std::vector<int> result;
    std::unordered_set<int> seen;
    std::istringstream input(manifest_text.substr(open + 1, close - open - 1));
    std::string token;
    while (std::getline(input, token, ',')) {
        token.erase(
            std::remove_if(token.begin(), token.end(), [](unsigned char ch) {
                return std::isspace(ch) != 0;
            }),
            token.end()
        );
        if (token.empty()) continue;
        std::size_t consumed = 0;
        const int node_id = std::stoi(token, &consumed);
        if (consumed != token.size() || node_id < 0 || !seen.insert(node_id).second) {
            throw std::invalid_argument("frozen placement node IDs are invalid or duplicated");
        }
        result.push_back(node_id);
    }
    if (result.empty()) {
        throw std::invalid_argument("frozen placement manifest selects no nodes");
    }
    return result;
}

struct MeshCsvRow {
    std::string experiment_id;
    std::string dataset;
    std::string algorithm;
    std::string param_name;
    std::string param_value;
    std::string query_tag;
    int k = 0;
    int tau = 0;
    int quant_bits = 0;
    int ef_search = 0;
    double build_time_ms = 0.0;
    double index_size_mb = 0.0;
    double avg_query_ms = 0.0;
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    double qps = 0.0;
    double recall_score = 0.0;
    double recall_id = 0.0;
    std::size_t indexed_block_groups = 0;
    std::size_t indexed_aspects = 0;
    std::string status;
};

inline void validateCsvText(const std::string& name, const std::string& value) {
    if (value.empty()) throw std::invalid_argument(name + " must be non-empty");
    if (value.find(',') != std::string::npos) {
        throw std::invalid_argument(name + " contains a comma");
    }
    if (value.find('\n') != std::string::npos || value.find('\r') != std::string::npos) {
        throw std::invalid_argument(name + " contains a newline");
    }
}

inline std::string serializeMeshCsvRow(const MeshCsvRow& row) {
    validateCsvText("experiment_id", row.experiment_id);
    validateCsvText("dataset", row.dataset);
    validateCsvText("algorithm", row.algorithm);
    validateCsvText("param_name", row.param_name);
    validateCsvText("param_value", row.param_value);
    validateCsvText("query_tag", row.query_tag);
    validateCsvText("status", row.status);
    if (row.algorithm != "Mesh-Road") {
        throw std::invalid_argument("Mesh CSV algorithm must be Mesh-Road");
    }
    if (row.status.rfind("ok", 0) != 0) {
        throw std::invalid_argument("Mesh CSV status must start with ok");
    }
    if (row.k <= 0 || row.tau < 0 || row.ef_search <= 0) {
        throw std::invalid_argument("Mesh CSV integer parameters are invalid");
    }
    const double numeric[] = {
        row.build_time_ms, row.index_size_mb, row.avg_query_ms, row.p50_ms,
        row.p95_ms, row.qps, row.recall_score, row.recall_id,
    };
    for (double value : numeric) {
        if (!std::isfinite(value)) throw std::invalid_argument("Mesh CSV numeric value must be finite");
    }
    if (row.recall_score < 0.0 || row.recall_score > 1.000001 ||
        row.recall_id < 0.0 || row.recall_id > 1.000001) {
        throw std::invalid_argument("Mesh CSV recall is outside [0,1]");
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(8)
        << row.experiment_id << ',' << row.dataset << ',' << row.algorithm
        << ',' << row.param_name << ',' << row.param_value << ',' << row.query_tag
        << ',' << row.k << ',' << row.tau << ',' << row.quant_bits << ',' << row.ef_search
        << ',' << row.build_time_ms << ',' << row.index_size_mb
        << ',' << row.avg_query_ms << ',' << row.p50_ms << ',' << row.p95_ms
        << ',' << row.qps << ',' << row.recall_score << ',' << row.recall_id
        << ',' << row.indexed_block_groups << ',' << row.indexed_aspects
        << ',' << row.status;
    return out.str();
}

inline std::size_t countCsvFields(const std::string& line) {
    if (line.empty()) return 0;
    return 1 + static_cast<std::size_t>(std::count(line.begin(), line.end(), ','));
}

template <class SearchFn>
inline auto measureOuterMilliseconds(SearchFn search_fn) {
    const auto begin = std::chrono::steady_clock::now();
    auto result = search_fn();
    const auto end = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double, std::milli>(end - begin).count();
    return std::make_pair(std::move(result), elapsed);
}

}  // namespace meshroad
