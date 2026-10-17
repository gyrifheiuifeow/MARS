#pragma once
// ---------------------------------------------------------------------------
// MARS-CAT unique-aspect (POI name) channel: theta early-stop SymphonyQG.
//
// Isolation discipline (hard constraint): this is a COPY of the upstream
// baselines/RaBitQ-Library-main/include/rabitqlib/index/symqg/qg.hpp, placed
// under mars_cat/src/ and modified ONLY here. The original qg.hpp is NOT
// touched. To avoid any ODR / redefinition clash with the original
// rabitqlib::symqg::QuantizedGraph (which the experiment runner already
// includes & uses for the global graph), this copy lives in a SEPARATE
// namespace rabitqlib::symqg_theta and is named QuantizedGraphTheta.
//
// We DO NOT re-implement the builder. The name graph is built with the
// original rabitqlib::symqg::QuantizedGraph + QGBuilder, then save()d to a
// binary file; QuantizedGraphTheta::load() reads that exact same binary
// format (identical layout, since this class is a verbatim structural copy)
// and we only run search on it here.
//
// New method: search_theta_stop (Plan A, true early stop, NO budget).
//   Replaces the ef-bounded while loop of search_filtered with a theta-driven
//   admissible early stop. METRIC_IP distance is dist = 1 - IP (see
//   utils/space.hpp dot_product_dis and index/query.hpp BatchQuery::set_g_add),
//   so similarity = 1 - dist and is monotonically DECREASING in distance.
//   search_pool is a min-by-distance beam; its front (next unchecked) gives the
//   smallest remaining distance, i.e. the LARGEST possible remaining similarity
//   = an upper bound on the name-similarity of every candidate still reachable.
//   Once that frontier upper bound <= theta, no remaining object can beat the
//   current k-th exact score -> we break. theta is supplied by the caller (the
//   shared cross-channel theta = current exact k-th objectScore). We report the
//   visited candidate ids (those accepted by the feasibility filter) plus their
//   name similarities so the integrator can decide which objects to exact-score.
// ---------------------------------------------------------------------------

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

// A single (object/vertex id, name-similarity) the theta-stop search surfaced.
template <typename T>
struct ThetaHit {
    uint32_t id;
    T similarity;  // exact IP between query and this name vector (= 1 - dist)
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
    void save(const char*) const; // Same persisted format; round-trip acceptance only.
    void set_ef(size_t cur_ef) { this->ef_ = cur_ef; }

    // ---- Plan A theta early-stop search (no budget) -----------------------
    // Runs best-first beam exactly like search_filtered, but the loop breaks as
    // soon as the search frontier's best POSSIBLE name-similarity (1 - smallest
    // remaining distance) drops to <= theta. For every popped node it computes
    // the EXACT name-IP (via raw_dist_func_) and, if accept(id) passes, records
    // a ThetaHit. The caller then exact-objectScores the accepted ids.
    //
    // theta : shared cross-channel threshold (current exact k-th objectScore).
    // accept: feasibility predicate (object in feasible road region).
    // hits  : output, accepted candidates with their exact name similarity.
    // steps : output, number of nodes popped (for diagnostics / navigability).
    template <class AcceptFn>
    void search_theta_stop(
        const T* __restrict__ query,
        T theta,
        AcceptFn accept,
        std::vector<ThetaHit<T>>& hits,
        size_t& steps
    );

    // ---- Standard SymphonyQG convergent search + theta filter -------------
    // FIX for the early-stop bug: the old search_theta_stop broke the beam as
    // soon as the SEARCH FRONTIER's similarity dropped to <= theta. That is the
    // classic HNSW/beam climb-phase trap: while the beam is still climbing from
    // entry_point toward the query, the frontier nodes are genuinely far (low
    // similarity), but continuing the walk reaches MUCH closer (higher
    // similarity) nodes. "Frontier sim <= theta -> break" therefore stops during
    // the climb, before the beam ever reaches the query's true name neighbors
    // (route_graph=1, feasible_object_checks~0, recall unchanged).
    //
    // search_converge instead runs the SAME convergence loop as upstream
    // search/search_filtered: a beam of fixed width ef_ (SymphonyQG's intrinsic
    // search-width parameter, NOT a budget and NOT tau-dependent) that runs
    // until the beam naturally converges (search_pool, capacity ef_, stops
    // inserting nodes farther than its worst, so has_next() drains to false).
    // This finds the query's true name top-neighbors. theta is then applied as a
    // FILTER on the converged candidates (keep only name similarity > theta),
    // never to interrupt the beam. The caller exact-objectScores the kept ids;
    // theta and the top-k heap are updated ONLY by exact objectScore.
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

        // exact distance of cur_node: dist = 1 - IP (METRIC_IP). similarity = 1 - dist.
        q_obj.set_g_add(raw_dist_func_(query, get_vector(cur_node), dim_));
        T cur_dist = q_obj.g_add();
        T cur_sim = static_cast<T>(1) - cur_dist;

        scan_neighbors(
            q_obj, cur_node, est_dist.data(), search_pool, *vis, this->degree_bound_
        );

        if (accept(cur_node)) {
            hits.push_back(ThetaHit<T>{static_cast<uint32_t>(cur_node), cur_sim});
        }

        // Admissible theta early stop (Plan A, no budget): the search frontier's
        // best remaining candidate is the unchecked node with the SMALLEST
        // distance. Its similarity (1 - that distance) upper-bounds the name
        // similarity of EVERY candidate still reachable along the beam. Once that
        // upper bound <= theta, no remaining object can raise the exact top-k
        // (name IP is an exact per-object upper bound on objectScore for the
        // name aspect, and objectScore is max over aspects), so we stop.
        if (search_pool.has_next()) {
            PID nxt = search_pool.next_id();
            // exact frontier distance (tighter & guaranteed vs. the quantized
            // estimate stored in the beam; keeps the stop admissible).
            T front_dist = raw_dist_func_(query, get_vector(nxt), dim_);
            T front_sim_ub = static_cast<T>(1) - front_dist;
            if (front_sim_ub <= theta) {
                break;
            }
        }
    }

    visited_list_pool_->release_vis_list(vis);
}

// Standard SymphonyQG convergent beam search + theta filter (the FIX).
// Mirrors upstream search_filtered's convergence loop EXACTLY (beam of width
// ef_, runs until search_pool drains by natural convergence), but:
//   - records, for every popped node, the EXACT name-IP similarity (= 1 - dist);
//   - keeps a candidate only if accept(id) passes (feasible road region) AND
//     its name similarity > theta (the theta FILTER on converged neighbors).
// theta NEVER interrupts the beam. The caller exact-objectScores the kept ids.
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

    // Standard convergence: no theta break. search_pool has fixed capacity ef_;
    // scan_neighbors stops inserting nodes farther than the current worst once
    // the pool is full, so the beam converges to the query's true ef_ nearest
    // name neighbors and has_next() naturally drains to false.
    while (search_pool.has_next()) {
        PID cur_node = search_pool.pop();
        if (vis->get(cur_node)) {
            continue;
        }
        vis->set(cur_node);
        ++steps;

        // exact name-IP for cur_node: dist = 1 - IP (METRIC_IP); sim = 1 - dist.
        q_obj.set_g_add(raw_dist_func_(query, get_vector(cur_node), dim_));
        T cur_dist = q_obj.g_add();
        T cur_sim = static_cast<T>(1) - cur_dist;

        scan_neighbors(
            q_obj, cur_node, est_dist.data(), search_pool, *vis, this->degree_bound_
        );

        // theta FILTER (not an early stop): keep feasible candidates whose name
        // similarity beats the current cross-channel theta. These are the name
        // neighbors that can still raise the exact top-k.
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
