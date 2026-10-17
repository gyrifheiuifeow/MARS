#pragma once

#include <string>

namespace marsbaseline {

inline void stripTrailingCarriageReturn(std::string& value) {
    if (!value.empty() && value.back() == '\r') value.pop_back();
}

}  // namespace marsbaseline
