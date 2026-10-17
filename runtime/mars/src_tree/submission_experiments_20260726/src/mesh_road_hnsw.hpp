#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

#include "rabitqlib/third/hnswlib/hnswlib.h"

namespace meshroad {

struct LocalAspect {
    int object_id = -1;
    int category_id = 0;
    bool is_category = false;
};

class LocalHNSW {
   public:
    LocalHNSW() = default;
    LocalHNSW(LocalHNSW&&) noexcept = default;
    LocalHNSW& operator=(LocalHNSW&&) noexcept = default;
    LocalHNSW(const LocalHNSW&) = delete;
    LocalHNSW& operator=(const LocalHNSW&) = delete;

    void build(
        const std::vector<float>& matrix,
        std::size_t rows,
        std::size_t dim,
        const std::vector<LocalAspect>& aspects,
        const std::vector<std::vector<int>>& postings,
        int m,
        int ef_build,
        std::uint64_t seed
    ) {
        validateMetadata(matrix, rows, dim, aspects, postings, m, ef_build);
        dim_ = dim;
        aspects_ = aspects;
        postings_ = postings;
        space_ = std::make_unique<hnswlib::InnerProductSpace>(dim_);
        index_ = std::make_unique<hnswlib::HierarchicalNSW<float>>(
            space_.get(), rows, static_cast<std::size_t>(m),
            static_cast<std::size_t>(ef_build), static_cast<std::size_t>(seed)
        );
        for (std::size_t row = 0; row < rows; ++row) {
            index_->addPoint(matrix.data() + row * dim_, row);
        }
        serialized_bytes_ = 0;
    }

    std::vector<std::size_t> search(
        const std::vector<float>& query,
        int ef,
        std::size_t limit
    ) {
        if (!index_ || !space_) throw std::logic_error("local HNSW is not loaded");
        if (query.size() != dim_) throw std::invalid_argument("query dimension mismatch");
        if (ef <= 0) throw std::invalid_argument("ef must be positive");
        limit = std::min(limit, aspects_.size());
        if (limit == 0) return {};
        index_->setEf(std::max<std::size_t>(static_cast<std::size_t>(ef), limit));
        auto heap = index_->searchKnn(query.data(), limit);
        std::vector<std::pair<float, std::size_t>> ranked;
        ranked.reserve(heap.size());
        while (!heap.empty()) {
            ranked.push_back({heap.top().first, static_cast<std::size_t>(heap.top().second)});
            heap.pop();
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.first != rhs.first) return lhs.first < rhs.first;
            return lhs.second < rhs.second;
        });
        std::vector<std::size_t> labels;
        labels.reserve(ranked.size());
        for (const auto& item : ranked) labels.push_back(item.second);
        return labels;
    }

    std::vector<int> expandObjects(const std::vector<std::size_t>& aspect_ids) const {
        std::unordered_set<int> seen;
        std::vector<int> objects;
        for (std::size_t aspect_id : aspect_ids) {
            if (aspect_id >= postings_.size()) {
                throw std::out_of_range("local aspect ID is outside postings");
            }
            for (int object_id : postings_[aspect_id]) {
                if (seen.insert(object_id).second) objects.push_back(object_id);
            }
        }
        return objects;
    }

    void save(const std::filesystem::path& prefix) {
        if (!index_) throw std::logic_error("local HNSW is not built");
        if (!prefix.parent_path().empty()) std::filesystem::create_directories(prefix.parent_path());
        const auto hnsw_path = prefix.string() + ".hnsw";
        const auto meta_path = prefix.string() + ".meta";
        index_->saveIndex(hnsw_path);

        std::ofstream out(meta_path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write local HNSW metadata");
        writeScalar(out, kMagic);
        writeScalar(out, static_cast<std::uint64_t>(dim_));
        writeScalar(out, static_cast<std::uint64_t>(aspects_.size()));
        for (std::size_t i = 0; i < aspects_.size(); ++i) {
            writeScalar(out, static_cast<std::int32_t>(aspects_[i].object_id));
            writeScalar(out, static_cast<std::int32_t>(aspects_[i].category_id));
            writeScalar(out, static_cast<std::uint8_t>(aspects_[i].is_category ? 1 : 0));
            writeScalar(out, static_cast<std::uint64_t>(postings_[i].size()));
            for (int object_id : postings_[i]) {
                writeScalar(out, static_cast<std::int32_t>(object_id));
            }
        }
        out.close();
        if (!out) throw std::runtime_error("failed writing local HNSW metadata");
        serialized_bytes_ = std::filesystem::file_size(hnsw_path) +
                            std::filesystem::file_size(meta_path);
    }

    void load(const std::filesystem::path& prefix) {
        const auto hnsw_path = prefix.string() + ".hnsw";
        const auto meta_path = prefix.string() + ".meta";
        std::ifstream in(meta_path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot read local HNSW metadata");
        if (readScalar<std::uint64_t>(in) != kMagic) {
            throw std::runtime_error("invalid local HNSW metadata magic");
        }
        dim_ = static_cast<std::size_t>(readScalar<std::uint64_t>(in));
        const auto count = static_cast<std::size_t>(readScalar<std::uint64_t>(in));
        if (dim_ == 0 || count == 0) throw std::runtime_error("invalid local HNSW dimensions");
        aspects_.clear();
        postings_.clear();
        aspects_.reserve(count);
        postings_.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            LocalAspect aspect;
            aspect.object_id = readScalar<std::int32_t>(in);
            aspect.category_id = readScalar<std::int32_t>(in);
            aspect.is_category = readScalar<std::uint8_t>(in) != 0;
            const auto posting_count = static_cast<std::size_t>(readScalar<std::uint64_t>(in));
            std::vector<int> posting;
            posting.reserve(posting_count);
            for (std::size_t j = 0; j < posting_count; ++j) {
                posting.push_back(readScalar<std::int32_t>(in));
            }
            aspects_.push_back(aspect);
            postings_.push_back(std::move(posting));
        }
        if (!in) throw std::runtime_error("truncated local HNSW metadata");

        space_ = std::make_unique<hnswlib::InnerProductSpace>(dim_);
        index_ = std::make_unique<hnswlib::HierarchicalNSW<float>>(space_.get(), hnsw_path);
        if (index_->getCurrentElementCount() != aspects_.size()) {
            throw std::runtime_error("local HNSW index/metadata count mismatch");
        }
        serialized_bytes_ = std::filesystem::file_size(hnsw_path) +
                            std::filesystem::file_size(meta_path);
    }

    std::uint64_t serializedBytes() const { return serialized_bytes_; }
    std::size_t size() const { return aspects_.size(); }
    std::size_t dim() const { return dim_; }

   private:
    static constexpr std::uint64_t kMagic = 0x4d455348524f4144ULL;
    std::size_t dim_ = 0;
    std::vector<LocalAspect> aspects_;
    std::vector<std::vector<int>> postings_;
    std::unique_ptr<hnswlib::InnerProductSpace> space_;
    std::unique_ptr<hnswlib::HierarchicalNSW<float>> index_;
    std::uint64_t serialized_bytes_ = 0;

    static void validateMetadata(
        const std::vector<float>& matrix,
        std::size_t rows,
        std::size_t dim,
        const std::vector<LocalAspect>& aspects,
        const std::vector<std::vector<int>>& postings,
        int m,
        int ef_build
    ) {
        if (rows == 0 || dim == 0 || matrix.size() != rows * dim) {
            throw std::invalid_argument("local HNSW matrix dimensions are invalid");
        }
        if (aspects.size() != rows || postings.size() != rows) {
            throw std::invalid_argument("local HNSW aspect/posting counts do not match rows");
        }
        if (m <= 0 || ef_build <= 0) throw std::invalid_argument("HNSW build parameters must be positive");
        for (std::size_t i = 0; i < rows; ++i) {
            if (postings[i].empty()) throw std::invalid_argument("local aspect posting is empty");
            for (int object_id : postings[i]) {
                if (object_id < 0) throw std::invalid_argument("local posting object ID is negative");
            }
            if (aspects[i].is_category) {
                if (aspects[i].object_id != -1 || aspects[i].category_id <= 0) {
                    throw std::invalid_argument("category aspect metadata is invalid");
                }
            } else if (aspects[i].object_id < 0 ||
                       std::find(postings[i].begin(), postings[i].end(), aspects[i].object_id) == postings[i].end()) {
                throw std::invalid_argument("POI aspect metadata/posting mismatch");
            }
        }
    }

    template <class T>
    static void writeScalar(std::ofstream& out, T value) {
        out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    template <class T>
    static T readScalar(std::ifstream& in) {
        T value{};
        in.read(reinterpret_cast<char*>(&value), sizeof(T));
        if (!in) throw std::runtime_error("truncated local HNSW metadata");
        return value;
    }
};

}  // namespace meshroad
