#pragma once
#include <cstdlib>
#include <filesystem>
inline std::filesystem::path publicationOutputRoot(const std::filesystem::path& fallback) {
    const char* p = std::getenv("MARS_OUTPUT_ROOT");
    return p && *p ? std::filesystem::path(p) : fallback;
}
