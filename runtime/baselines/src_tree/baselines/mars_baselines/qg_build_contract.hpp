#pragma once

#include "rabitqlib/index/symqg/qg_builder.hpp"

#include <cstdint>
#include <stdexcept>

namespace reviewer_contract {

inline std::uint32_t requireSIMDSafeQGDegree(std::uint32_t degree) {
    constexpr std::uint32_t SIMD_BATCH = 32;
    if (degree == 0 || degree % SIMD_BATCH != 0) {
        throw std::invalid_argument("qg_degree must be a positive multiple of 32");
    }
    return degree;
}

inline rabitqlib::symqg::QGBuilder makeSingleThreadQGBuilder(
    rabitqlib::symqg::QuantizedGraph<float>& graph,
    std::uint32_t ef_build,
    const float* data
) {
    return rabitqlib::symqg::QGBuilder(graph, ef_build, data, 1);
}

}  // namespace reviewer_contract
