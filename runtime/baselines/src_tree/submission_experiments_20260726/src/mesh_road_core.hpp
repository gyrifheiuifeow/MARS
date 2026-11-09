#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace meshroad {

struct MeshPoint {
    int object_id = -1;
    int vertex_id = -1;
    double x = 0.0;
    double y = 0.0;
};

struct RoadEdgeCoordinate {
    double weight = 0.0;
    double ux = 0.0;
    double uy = 0.0;
    double vx = 0.0;
    double vy = 0.0;
};

struct BoundingBox {
    double min_x = 0.0;
    double min_y = 0.0;
    double max_x = 0.0;
    double max_y = 0.0;
};

struct STRNode {
    int id = -1;
    int parent = -1;
    bool leaf = false;
    BoundingBox box;
    std::vector<int> children;
    std::vector<int> object_ids;
};

inline double certifiedAlpha(const std::vector<RoadEdgeCoordinate>& edges) {
    double minimum = std::numeric_limits<double>::infinity();
    for (const auto& edge : edges) {
        if (!std::isfinite(edge.weight) || edge.weight <= 0.0 ||
            !std::isfinite(edge.ux) || !std::isfinite(edge.uy) ||
            !std::isfinite(edge.vx) || !std::isfinite(edge.vy)) {
            throw std::invalid_argument("road edge coordinates and weight must be finite and positive");
        }
        const double dx = edge.ux - edge.vx;
        const double dy = edge.uy - edge.vy;
        const double length = std::hypot(dx, dy);
        if (length == 0.0) continue;
        minimum = std::min(minimum, edge.weight / length);
    }
    if (!std::isfinite(minimum) || minimum <= 0.0) {
        throw std::invalid_argument("no non-degenerate road edge is available for certified alpha");
    }
    return std::nextafter(minimum, 0.0);
}

inline double certifiedRadius(double tau, double alpha) {
    if (!std::isfinite(tau) || tau < 0.0 || !std::isfinite(alpha) || alpha <= 0.0) {
        throw std::invalid_argument("tau and alpha must define a finite non-negative radius");
    }
    const double radius = tau / alpha;
    if (!std::isfinite(radius)) throw std::overflow_error("certified radius overflow");
    return radius;
}

inline bool boxIntersectsCircle(
    const BoundingBox& box,
    double center_x,
    double center_y,
    double radius
) {
    if (!std::isfinite(center_x) || !std::isfinite(center_y) ||
        !std::isfinite(radius) || radius < 0.0) {
        throw std::invalid_argument("circle must be finite with a non-negative radius");
    }
    const double nearest_x = std::max(box.min_x, std::min(center_x, box.max_x));
    const double nearest_y = std::max(box.min_y, std::min(center_y, box.max_y));
    const double dx = nearest_x - center_x;
    const double dy = nearest_y - center_y;
    return dx * dx + dy * dy <= radius * radius;
}

inline bool boxInsideCircle(
    const BoundingBox& box,
    double center_x,
    double center_y,
    double radius
) {
    const double radius_sq = radius * radius;
    const double xs[2] = {box.min_x, box.max_x};
    const double ys[2] = {box.min_y, box.max_y};
    for (double x : xs) {
        for (double y : ys) {
            const double dx = x - center_x;
            const double dy = y - center_y;
            if (dx * dx + dy * dy > radius_sq) return false;
        }
    }
    return true;
}

class STRTree {
   public:
    void build(const std::vector<MeshPoint>& input, std::size_t leaf_capacity) {
        if (leaf_capacity == 0) throw std::invalid_argument("leaf capacity must be positive");
        nodes_.clear();
        root_id_ = -1;
        if (input.empty()) return;

        std::unordered_set<int> object_ids;
        std::vector<MeshPoint> points = input;
        for (const auto& point : points) {
            if (point.object_id < 0 || !object_ids.insert(point.object_id).second) {
                throw std::invalid_argument("object IDs must be unique and non-negative");
            }
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
                throw std::invalid_argument("point coordinates must be finite");
            }
        }

        const auto by_x = [](const MeshPoint& lhs, const MeshPoint& rhs) {
            if (lhs.x != rhs.x) return lhs.x < rhs.x;
            if (lhs.y != rhs.y) return lhs.y < rhs.y;
            return lhs.object_id < rhs.object_id;
        };
        const auto by_y = [](const MeshPoint& lhs, const MeshPoint& rhs) {
            if (lhs.y != rhs.y) return lhs.y < rhs.y;
            if (lhs.x != rhs.x) return lhs.x < rhs.x;
            return lhs.object_id < rhs.object_id;
        };
        std::sort(points.begin(), points.end(), by_x);

        const std::size_t leaf_count = (points.size() + leaf_capacity - 1) / leaf_capacity;
        const std::size_t slice_count = static_cast<std::size_t>(
            std::ceil(std::sqrt(static_cast<double>(leaf_count)))
        );
        const std::size_t slice_size = (points.size() + slice_count - 1) / slice_count;
        std::vector<int> level;
        for (std::size_t slice_begin = 0; slice_begin < points.size(); slice_begin += slice_size) {
            const std::size_t slice_end = std::min(points.size(), slice_begin + slice_size);
            std::sort(points.begin() + slice_begin, points.begin() + slice_end, by_y);
            for (std::size_t begin = slice_begin; begin < slice_end; begin += leaf_capacity) {
                const std::size_t end = std::min(slice_end, begin + leaf_capacity);
                STRNode node;
                node.id = static_cast<int>(nodes_.size());
                node.leaf = true;
                node.box = pointBox(points[begin]);
                for (std::size_t index = begin; index < end; ++index) {
                    node.object_ids.push_back(points[index].object_id);
                    expand(node.box, pointBox(points[index]));
                }
                nodes_.push_back(std::move(node));
                level.push_back(static_cast<int>(nodes_.size() - 1));
            }
        }

        const std::size_t fanout = std::max<std::size_t>(2, slice_count);
        while (level.size() > 1) {
            std::vector<int> next;
            for (std::size_t begin = 0; begin < level.size(); begin += fanout) {
                const std::size_t end = std::min(level.size(), begin + fanout);
                STRNode parent;
                parent.id = static_cast<int>(nodes_.size());
                parent.leaf = false;
                parent.box = nodes_[level[begin]].box;
                for (std::size_t index = begin; index < end; ++index) {
                    const int child_id = level[index];
                    parent.children.push_back(child_id);
                    parent.object_ids.insert(
                        parent.object_ids.end(),
                        nodes_[child_id].object_ids.begin(),
                        nodes_[child_id].object_ids.end()
                    );
                    expand(parent.box, nodes_[child_id].box);
                }
                nodes_.push_back(std::move(parent));
                const int parent_id = static_cast<int>(nodes_.size() - 1);
                for (int child_id : nodes_[parent_id].children) nodes_[child_id].parent = parent_id;
                next.push_back(parent_id);
            }
            level = std::move(next);
        }
        root_id_ = level.front();
    }

    std::vector<int> cover(double center_x, double center_y, double radius) const {
        std::vector<int> result;
        if (root_id_ < 0) return result;
        coverNode(root_id_, center_x, center_y, radius, result);
        return result;
    }

    template <class IndexedSet>
    std::vector<int> executionCover(
        double center_x,
        double center_y,
        double radius,
        const IndexedSet& indexed_nodes
    ) const {
        std::vector<int> result;
        if (root_id_ < 0) return result;
        executionCoverNode(
            root_id_, center_x, center_y, radius, indexed_nodes, result
        );
        return result;
    }

    const std::vector<STRNode>& nodes() const { return nodes_; }

    const std::vector<int>& objects(int node_id) const {
        if (node_id < 0 || node_id >= static_cast<int>(nodes_.size())) {
            throw std::out_of_range("invalid STR node ID");
        }
        return nodes_[node_id].object_ids;
    }

    int rootId() const { return root_id_; }

    // Restore the root and node links from a saved STR tree.
    void restoreFrozen(int root, std::vector<STRNode> nodes) {
        if (root < 0 || root >= static_cast<int>(nodes.size())) throw std::runtime_error("bad saved STR root");
        for (size_t i=0;i<nodes.size();++i) {
            if(nodes[i].id!=static_cast<int>(i))throw std::runtime_error("bad saved STR node ID");
            for(int child:nodes[i].children)if(child<0||child>=static_cast<int>(nodes.size()))throw std::runtime_error("bad saved STR edge");
        }
        root_id_=root;nodes_=std::move(nodes);
    }


    std::string signature() const {
        std::ostringstream out;
        out << std::setprecision(17) << "root=" << root_id_ << ';';
        for (const auto& node : nodes_) {
            out << node.id << ':' << node.parent << ':' << node.leaf << ':'
                << node.box.min_x << ':' << node.box.min_y << ':'
                << node.box.max_x << ':' << node.box.max_y << ':';
            for (int child : node.children) out << 'c' << child << ',';
            for (int object_id : node.object_ids) out << 'o' << object_id << ',';
            out << ';';
        }
        return out.str();
    }

   private:
    std::vector<STRNode> nodes_;
    int root_id_ = -1;

    static BoundingBox pointBox(const MeshPoint& point) {
        return {point.x, point.y, point.x, point.y};
    }

    static void expand(BoundingBox& box, const BoundingBox& other) {
        box.min_x = std::min(box.min_x, other.min_x);
        box.min_y = std::min(box.min_y, other.min_y);
        box.max_x = std::max(box.max_x, other.max_x);
        box.max_y = std::max(box.max_y, other.max_y);
    }

    void coverNode(
        int node_id,
        double center_x,
        double center_y,
        double radius,
        std::vector<int>& result
    ) const {
        const auto& node = nodes_[node_id];
        if (!boxIntersectsCircle(node.box, center_x, center_y, radius)) return;
        if (node.leaf || boxInsideCircle(node.box, center_x, center_y, radius)) {
            result.push_back(node_id);
            return;
        }
        for (int child_id : node.children) {
            coverNode(child_id, center_x, center_y, radius, result);
        }
    }

    template <class IndexedSet>
    void executionCoverNode(
        int node_id,
        double center_x,
        double center_y,
        double radius,
        const IndexedSet& indexed_nodes,
        std::vector<int>& result
    ) const {
        const auto& node = nodes_[node_id];
        if (!boxIntersectsCircle(node.box, center_x, center_y, radius)) return;
        const bool indexed = indexed_nodes.count(node_id) != 0;
        if (node.leaf || (indexed && boxInsideCircle(node.box, center_x, center_y, radius))) {
            result.push_back(node_id);
            return;
        }
        for (int child_id : node.children) {
            executionCoverNode(
                child_id, center_x, center_y, radius, indexed_nodes, result
            );
        }
    }
};

}  // namespace meshroad
