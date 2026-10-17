#pragma once

#include "mesh_road_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace meshroad {

inline bool placementCoversQuery(
    const BoundingBox& box,
    double center_x,
    double center_y,
    double radius
) {
    return boxInsideCircle(box, center_x, center_y, radius);
}

struct PlacementCandidate {
    int node_id = -1;
    std::uint64_t estimated_bytes = 0;
    double saved_scores = 0.0;
};

struct PlacementWorkloadPoint {
    double center_x = 0.0;
    double center_y = 0.0;
    double radius = 0.0;
    double weight = 1.0;
};

struct NodeWorkEstimate {
    int node_id = -1;
    std::uint64_t estimated_bytes = 0;
    double exact_work = 0.0;
    double indexed_work = 0.0;
};

struct PlacementSelection {
    std::vector<int> node_ids;
    std::uint64_t used_bytes = 0;
    double initial_work = 0.0;
    double final_work = 0.0;
};

inline std::unordered_map<int, NodeWorkEstimate> validateWorkEstimates(
    const STRTree& tree,
    const std::vector<PlacementWorkloadPoint>& workload,
    const std::vector<NodeWorkEstimate>& estimates
) {
    if (workload.empty()) {
        throw std::invalid_argument("placement workload must be non-empty");
    }
    for (const auto& point : workload) {
        if (!std::isfinite(point.center_x) || !std::isfinite(point.center_y) ||
            !std::isfinite(point.radius) || point.radius < 0.0 ||
            !std::isfinite(point.weight) || point.weight <= 0.0) {
            throw std::invalid_argument("placement workload point is invalid");
        }
    }

    std::unordered_map<int, NodeWorkEstimate> by_node;
    for (const auto& estimate : estimates) {
        if (estimate.node_id < 0 ||
            estimate.node_id >= static_cast<int>(tree.nodes().size()) ||
            !by_node.emplace(estimate.node_id, estimate).second) {
            throw std::invalid_argument("placement work estimate has an invalid or duplicate node ID");
        }
        if (estimate.estimated_bytes == 0 ||
            !std::isfinite(estimate.exact_work) || estimate.exact_work < 0.0 ||
            !std::isfinite(estimate.indexed_work) || estimate.indexed_work < 0.0) {
            throw std::invalid_argument("placement work estimate is invalid");
        }
    }
    if (by_node.size() != tree.nodes().size()) {
        throw std::invalid_argument("placement work estimates must cover every STR node");
    }
    return by_node;
}

template <class SelectedSet>
inline double predictedPlacementWork(
    const STRTree& tree,
    const std::vector<PlacementWorkloadPoint>& workload,
    const std::unordered_map<int, NodeWorkEstimate>& estimates,
    const SelectedSet& selected_nodes
) {
    double total = 0.0;
    for (const auto& point : workload) {
        const auto cover = tree.executionCover(
            point.center_x, point.center_y, point.radius, selected_nodes
        );
        double query_work = 0.0;
        for (int node_id : cover) {
            const auto found = estimates.find(node_id);
            if (found == estimates.end()) {
                throw std::invalid_argument("execution cover references an unestimated node");
            }
            query_work += selected_nodes.count(node_id) != 0
                ? found->second.indexed_work
                : found->second.exact_work;
        }
        total += point.weight * query_work;
    }
    if (!std::isfinite(total) || total < 0.0) {
        throw std::invalid_argument("predicted placement work is invalid");
    }
    return total;
}

inline PlacementSelection selectMarginalPlacements(
    const STRTree& tree,
    const std::vector<PlacementWorkloadPoint>& workload,
    const std::vector<NodeWorkEstimate>& estimates,
    std::uint64_t budget_bytes
) {
    const auto by_node = validateWorkEstimates(tree, workload, estimates);
    std::unordered_set<int> selected;
    PlacementSelection result;
    result.initial_work = predictedPlacementWork(tree, workload, by_node, selected);
    result.final_work = result.initial_work;

    while (result.used_bytes < budget_bytes) {
        int best_node = -1;
        double best_saved = 0.0;
        std::uint64_t best_bytes = 0;
        long double best_ratio = 0.0L;
        double best_total = result.final_work;

        for (const auto& node : tree.nodes()) {
            if (selected.count(node.id) != 0) continue;
            const auto& estimate = by_node.at(node.id);
            if (estimate.estimated_bytes > budget_bytes - result.used_bytes) continue;
            selected.insert(node.id);
            const double trial_total = predictedPlacementWork(
                tree, workload, by_node, selected
            );
            selected.erase(node.id);
            const double saved = result.final_work - trial_total;
            if (!(saved > 0.0)) continue;
            const long double ratio = static_cast<long double>(saved) /
                static_cast<long double>(estimate.estimated_bytes);
            const bool better = best_node < 0 || ratio > best_ratio ||
                (ratio == best_ratio && saved > best_saved) ||
                (ratio == best_ratio && saved == best_saved &&
                 estimate.estimated_bytes < best_bytes) ||
                (ratio == best_ratio && saved == best_saved &&
                 estimate.estimated_bytes == best_bytes && node.id < best_node);
            if (better) {
                best_node = node.id;
                best_saved = saved;
                best_bytes = estimate.estimated_bytes;
                best_ratio = ratio;
                best_total = trial_total;
            }
        }

        if (best_node < 0) break;
        selected.insert(best_node);
        result.node_ids.push_back(best_node);
        result.used_bytes += best_bytes;
        result.final_work = best_total;
    }
    return result;
}

inline std::vector<int> selectPlacements(
    const std::vector<PlacementCandidate>& input,
    std::uint64_t budget_bytes
) {
    std::vector<PlacementCandidate> candidates = input;
    std::unordered_set<int> node_ids;
    for (const auto& candidate : candidates) {
        if (candidate.node_id < 0 || !node_ids.insert(candidate.node_id).second) {
            throw std::invalid_argument("placement node IDs must be unique and non-negative");
        }
        if (candidate.estimated_bytes == 0) {
            throw std::invalid_argument("placement byte estimate must be positive");
        }
        if (!std::isfinite(candidate.saved_scores) || candidate.saved_scores < 0.0) {
            throw std::invalid_argument("placement saved-score estimate must be finite and non-negative");
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
        const long double lhs_ratio =
            static_cast<long double>(lhs.saved_scores) / lhs.estimated_bytes;
        const long double rhs_ratio =
            static_cast<long double>(rhs.saved_scores) / rhs.estimated_bytes;
        if (lhs_ratio != rhs_ratio) return lhs_ratio > rhs_ratio;
        if (lhs.saved_scores != rhs.saved_scores) return lhs.saved_scores > rhs.saved_scores;
        return lhs.node_id < rhs.node_id;
    });

    std::uint64_t used = 0;
    std::vector<int> selected;
    for (const auto& candidate : candidates) {
        if (candidate.saved_scores <= 0.0) continue;
        if (candidate.estimated_bytes > budget_bytes - used) continue;
        selected.push_back(candidate.node_id);
        used += candidate.estimated_bytes;
    }
    return selected;
}

inline void validateTrainingWorkloadTag(
    const std::string& training_tag,
    const std::string& evaluation_tag
) {
    if (training_tag.empty() || evaluation_tag.empty()) {
        throw std::invalid_argument("training and evaluation query tags must be non-empty");
    }
    if (training_tag == evaluation_tag) {
        throw std::invalid_argument("placement workload must be independent of evaluation queries");
    }
}

}  // namespace meshroad
