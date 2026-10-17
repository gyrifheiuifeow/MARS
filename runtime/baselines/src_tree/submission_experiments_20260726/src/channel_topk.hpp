#pragma once

#include <algorithm>
#include <cstddef>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ChannelTopK {
public:
    explicit ChannelTopK(int k) : k_(k > 0 ? static_cast<std::size_t>(k) : 0) {}

    bool update(int object_id, double score) {
        auto old = scores_.find(object_id);
        if (old != scores_.end() && score <= old->second) return false;

        if (old != scores_.end() && in_top_.erase(object_id) > 0) {
            top_.erase(Entry{old->second, object_id});
        }
        scores_[object_id] = score;

        if (k_ == 0) return true;
        Entry entry{score, object_id};
        if (top_.size() < k_) {
            top_.insert(entry);
            in_top_.insert(object_id);
            return true;
        }

        auto worst = top_.begin();
        if (better(entry, *worst)) {
            in_top_.erase(worst->object_id);
            top_.erase(worst);
            top_.insert(entry);
            in_top_.insert(object_id);
        }
        return true;
    }

    double theta() const {
        return top_.size() >= k_ && k_ > 0 ? top_.begin()->score : -1e100;
    }

    double score(int object_id) const {
        auto it = scores_.find(object_id);
        return it == scores_.end() ? -1e100 : it->second;
    }

    std::size_t observedObjectCount() const { return scores_.size(); }

    std::vector<int> ids() const {
        std::vector<Entry> entries(top_.begin(), top_.end());
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return better(a, b);
        });
        std::vector<int> result;
        result.reserve(entries.size());
        for (const Entry& entry : entries) result.push_back(entry.object_id);
        return result;
    }

private:
    struct Entry {
        double score;
        int object_id;
    };

    struct WorstFirst {
        bool operator()(const Entry& a, const Entry& b) const {
            if (a.score != b.score) return a.score < b.score;
            return a.object_id > b.object_id;
        }
    };

    static bool better(const Entry& a, const Entry& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.object_id < b.object_id;
    }

    std::size_t k_;
    std::unordered_map<int, double> scores_;
    std::unordered_set<int> in_top_;
    std::set<Entry, WorstFirst> top_;
};
