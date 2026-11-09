#pragma once
// SymphonyQG queries for the rest channel.
// Uses the upstream graph file format in a separate namespace.
// MARS calls search_converge for approximate beam search and score filtering.

#include <omp.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <ostream>
#include <vector>

#include "rabitqlib/defines.hpp"
#include "rabitqlib/fastscan/fastscan.hpp"
#include "rabitqlib/index/estimator.hpp"
#include "rabitqlib/index/query.hpp"
#include "rabitqlib/quantization/data_layout.hpp"
#include "rabitqlib/quantization/rabitq.hpp"
#include "rabitqlib/utils/array.hpp"
#include "rabitqlib/utils/buffer.hpp"
#include "rabitqlib/utils/hashset.hpp"
#include "rabitqlib/utils/io.hpp"
#include "rabitqlib/utils/memory.hpp"
#include "rabitqlib/utils/rotator.hpp"
#include "rabitqlib/utils/space.hpp"
#include "rabitqlib/utils/visited_pool.hpp"

namespace rabitqlib::symqg_theta {

// A visited graph node and its query inner product.
template <typename T>
struct ThetaHit {
    uint32_t id;
    T similarity;  // query inner product for this graph node
};

template <typename T = float>
class QuantizedGraphTheta {
   private:
    size_t num_points_ = 0;
    size_t degree_bound_ = 0;
    size_t dim_ = 0;
    size_t padded_dim_ = 0;
    T (*raw_dist_func_)(const T*, const T*, size_t) = nullptr;
    PID entry_point_ = 0;
    MetricType metric_type_ = MetricType::METRIC_L2;
    RotatorType rotator_type_ = RotatorType::FhtKacRotator;

    Array<
        char,
        std::vector<size_t>,
        memory::AlignedAllocator<char, 1 << 22, true>>
        data_;
    Rotator<T>* rotator_ = nullptr;
    std::unique_ptr<VisitedListPool> visited_list_pool_ = nullptr;

    size_t batch_data_offset_ = 0;
    size_t neighbor_offset_ = 0;
    size_t row_offset_ = 0;
    size_t ef_ = 0;

    void initialize();

    [[nodiscard]] T* get_vector(PID data_id) {
        return reinterpret_cast<T*>(&data_.at(row_offset_ * data_id));
    }
    [[nodiscard]] const T* get_vector(PID data_id) const {
        return reinterpret_cast<const T*>(&data_.at(row_offset_ * data_id));
    }
    [[nodiscard]] char* get_batch_data(PID data_id) {
        return &data_.at((row_offset_ * data_id) + batch_data_offset_);
    }
    [[nodiscard]] const char* get_batch_data(PID data_id) const {
        return &data_.at((row_offset_ * data_id) + batch_data_offset_);
    }
    [[nodiscard]] PID* get_neighbors(PID data_id) {
        return reinterpret_cast<PID*>(&data_.at((row_offset_ * data_id) + neighbor_offset_));
    }
    [[nodiscard]] const PID* get_neighbors(PID data_id) const {
        return reinterpret_cast<const PID*>(
            &data_.at((row_offset_ * data_id) + neighbor_offset_)
        );
    }

    void scan_neighbors(
        const BatchQuery<T>&,
        PID,
        T*,
        buffer::SearchBuffer<T>&,
        HashBasedBooleanSet&,
        size_t
    ) const;

   public:
    explicit QuantizedGraphTheta() = default;
    ~QuantizedGraphTheta();

    [[nodiscard]] auto num_vertices() const { return this->num_points_; }
    [[nodiscard]] auto dimension() const { return this->dim_; }
    [[nodiscard]] auto degree_bound() const { return this->degree_bound_; }
    [[nodiscard]] auto entry_point() const { return this->entry_point_; }

    void load(const char*);
    void save(const char*) const; // Uses the upstream graph serialization format.
    void set_ef(size_t cur_ef) { this->ef_ = cur_ef; }

    // Legacy search that stops when the next beam node has similarity <= theta.
    // The frontier score does not bound undiscovered nodes. MARS uses search_converge.
    // accept selects output nodes. steps counts popped nodes.
    template <class AcceptFn>
    void search_theta_stop(
        const T* __restrict__ query,
        T theta,
        AcceptFn accept,
        std::vector<ThetaHit<T>>& hits,
        size_t& steps
    );

    // Approximate beam search with capacity ef_. The beam drains without a theta stop.
    // Return popped nodes that pass accept and have inner product greater than theta.
    template <class AcceptFn>
    void search_converge(
        const T* __restrict__ query,
        T theta,
        AcceptFn accept,
        std::vector<ThetaHit<T>>& hits,
        size_t& steps
    );
};


template <typename T>
inline void QuantizedGraphTheta<T>::save(const char* filename) const {
    std::cout << "Saving quantized graph to " << filename << '\n';
    std::ofstream output(filename, std::ios::binary);
    assert(output.is_open());

    /* Basic variants */
    output.write(reinterpret_cast<const char*>(&num_points_), sizeof(size_t));
    output.write(reinterpret_cast<const char*>(&degree_bound_), sizeof(size_t));
    output.write(reinterpret_cast<const char*>(&dim_), sizeof(size_t));
    output.write(reinterpret_cast<const char*>(&padded_dim_), sizeof(size_t));
    output.write(reinterpret_cast<const char*>(&entry_point_), sizeof(PID));
    output.write(reinterpret_cast<const char*>(&rotator_type_), sizeof(RotatorType));
    output.write(reinterpret_cast<const char*>(&metric_type_), sizeof(MetricType));

    /* Data */
    data_.save(output);

    /* Rotator */
    this->rotator_->save(output);

    output.close();
    std::cout << "\tQuantized graph saved!\n";
}
template <typename T>
inline QuantizedGraphTheta<T>::~QuantizedGraphTheta() {
    ::delete this->rotator_;
}

template <typename T>
inline void QuantizedGraphTheta<T>::load(const char* filename) {
    std::cout << "[name-theta] loading quantized graph " << filename << '\n';
    if (!file_exists(filename)) {
        std::cerr << "[name-theta] Index does not exist: " << filename << "\n";
        exit(1);
    }
    std::ifstream input(filename, std::ios::binary);
    assert(input.is_open());

    input.read(reinterpret_cast<char*>(&num_points_), sizeof(size_t));
    input.read(reinterpret_cast<char*>(&degree_bound_), sizeof(size_t));
    input.read(reinterpret_cast<char*>(&dim_), sizeof(size_t));
    input.read(reinterpret_cast<char*>(&padded_dim_), sizeof(size_t));
    input.read(reinterpret_cast<char*>(&entry_point_), sizeof(PID));
    input.read(reinterpret_cast<char*>(&rotator_type_), sizeof(RotatorType));
    input.read(reinterpret_cast<char*>(&metric_type_), sizeof(MetricType));

    raw_dist_func_ = (metric_type_ == METRIC_IP) ? dot_product_dis<T> : euclidean_sqr<T>;

    initialize();

    data_.load(input);
    this->rotator_->load(input);
    if (rotator_->size() != padded_dim_) {
        std::cerr << "[name-theta] Bad padded_dim_ for rotator in load()\n";
        exit(1);
    }
    input.close();
    std::cout << "[name-theta] Quantized graph loaded! points=" << num_points_
              << " dim=" << dim_ << " deg=" << degree_bound_ << '\n';
}

template <typename T>
template <class AcceptFn>
inline void QuantizedGraphTheta<T>::search_theta_stop(
    const T* __restrict__ query,
    T theta,
    AcceptFn accept,
    std::vector<ThetaHit<T>>& hits,
    size_t& steps
) {
    hits.clear();
    steps = 0;

    std::vector<T> rotated_query(padded_dim_);
    rotator_->rotate(query, rotated_query.data());

    BatchQuery<T> q_obj(rotated_query.data(), padded_dim_);
    buffer::SearchBuffer<T> search_pool(ef_);
    search_pool.insert(this->entry_point_, std::numeric_limits<T>::max());

    auto* vis = visited_list_pool_->get_free_vislist();
    std::vector<T> est_dist(degree_bound_);

    while (search_pool.has_next()) {
        PID cur_node = search_pool.pop();
        if (vis->get(cur_node)) {
            continue;
        }
        vis->set(cur_node);
        ++steps;

        // Convert inner-product distance to similarity: sim = 1 - dist.
        q_obj.set_g_add(raw_dist_func_(query, get_vector(cur_node), dim_));
        T cur_dist = q_obj.g_add();
        T cur_sim = static_cast<T>(1) - cur_dist;

        scan_neighbors(
            q_obj, cur_node, est_dist.data(), search_pool, *vis, this->degree_bound_
        );

        if (accept(cur_node)) {
            hits.push_back(ThetaHit<T>{static_cast<uint32_t>(cur_node), cur_sim});
        }

        // Apply the legacy frontier cutoff. This score is not a global search bound.
        if (search_pool.has_next()) {
            PID nxt = search_pool.next_id();
            // Evaluate the next frontier node using its stored vector.
            T front_dist = raw_dist_func_(query, get_vector(nxt), dim_);
            T front_sim_ub = static_cast<T>(1) - front_dist;
            if (front_sim_ub <= theta) {
                break;
            }
        }
    }

    visited_list_pool_->release_vis_list(vis);
}

// Expand the approximate beam and collect road-feasible hits above theta.
template <typename T>
template <class AcceptFn>
inline void QuantizedGraphTheta<T>::search_converge(
    const T* __restrict__ query,
    T theta,
    AcceptFn accept,
    std::vector<ThetaHit<T>>& hits,
    size_t& steps
) {
    hits.clear();
    steps = 0;

    std::vector<T> rotated_query(padded_dim_);
    rotator_->rotate(query, rotated_query.data());

    BatchQuery<T> q_obj(rotated_query.data(), padded_dim_);
    buffer::SearchBuffer<T> search_pool(ef_);
    search_pool.insert(this->entry_point_, std::numeric_limits<T>::max());

    auto* vis = visited_list_pool_->get_free_vislist();
    std::vector<T> est_dist(degree_bound_);

    // Expand unchecked nodes until the fixed-capacity search pool drains.
    while (search_pool.has_next()) {
        PID cur_node = search_pool.pop();
        if (vis->get(cur_node)) {
            continue;
        }
        vis->set(cur_node);
        ++steps;

        // Convert inner-product distance to similarity: sim = 1 - dist.
        q_obj.set_g_add(raw_dist_func_(query, get_vector(cur_node), dim_));
        T cur_dist = q_obj.g_add();
        T cur_sim = static_cast<T>(1) - cur_dist;

        scan_neighbors(
            q_obj, cur_node, est_dist.data(), search_pool, *vis, this->degree_bound_
        );

        // Filter output hits while continuing graph expansion.
        if (cur_sim > theta && accept(cur_node)) {
            hits.push_back(ThetaHit<T>{static_cast<uint32_t>(cur_node), cur_sim});
        }
    }

    visited_list_pool_->release_vis_list(vis);
}


template <typename T>
void QuantizedGraphTheta<T>::scan_neighbors(
    const BatchQuery<T>& q_obj,
    PID data_id,
    T* est_dist,
    buffer::SearchBuffer<T>& search_pool,
    HashBasedBooleanSet& vis,
    size_t cur_degree
) const {
    const auto* batch_data = get_batch_data(data_id);
    for (size_t i = 0; i < cur_degree; i += fastscan::kBatchSize) {
        qg_batch_estdist(batch_data, q_obj, padded_dim_, est_dist + i);
        batch_data += QGBatchDataMap<T>::data_bytes(padded_dim_);
    }

    const PID* ptr_nb = get_neighbors(data_id);
    for (size_t i = 0; i < cur_degree; ++i) {
        PID cur_neighbor = ptr_nb[i];
        T dist = est_dist[i];
        if (search_pool.is_full(dist) || vis.get(cur_neighbor)) {
            continue;
        }
        search_pool.insert(cur_neighbor, dist);
        memory::mem_prefetch_l2(
            reinterpret_cast<const char*>(get_vector(search_pool.next_id())), 10
        );
    }
}

// verbatim copy of upstream initialize() (same data layout as original qg.hpp)
template <typename T>
inline void QuantizedGraphTheta<T>::initialize() {
    ::delete rotator_;

    rotator_ = choose_rotator<float>(dim_, rotator_type_, round_up_to_multiple(dim_, 64));
    padded_dim_ = rotator_->size();

    assert(padded_dim_ % 64 == 0);
    assert(padded_dim_ >= dim_);

    this->batch_data_offset_ = dim_ * sizeof(T);
    this->neighbor_offset_ =
        batch_data_offset_ + (QGBatchDataMap<T>::data_bytes(padded_dim_) *
                              (degree_bound_ / fastscan::kBatchSize));
    this->row_offset_ = neighbor_offset_ + (degree_bound_ * sizeof(PID));

    data_ = Array<char, std::vector<size_t>, memory::AlignedAllocator<char, 1 << 22, true>>(
        std::vector<size_t>{num_points_, row_offset_}
    );

    visited_list_pool_ = std::make_unique<VisitedListPool>(1, num_points_);
}

}  // namespace rabitqlib::symqg_theta
