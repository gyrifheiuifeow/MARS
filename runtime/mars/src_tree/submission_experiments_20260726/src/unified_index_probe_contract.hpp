#pragma once

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace indexprobe {

namespace fs = std::filesystem;

struct IndexSummary {
    std::uint64_t serialized_bytes = 0;
    std::string index_sha256;
};

inline std::string hexDigest(const unsigned char* digest, std::size_t size) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) {
        out << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return out.str();
}

inline std::string sha256Text(const std::string& value) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int size = 0;
    if (EVP_Digest(
            value.data(), value.size(), digest.data(), &size, EVP_sha256(), nullptr
        ) != 1) {
        throw std::runtime_error("SHA-256 digest failed");
    }
    return hexDigest(digest.data(), size);
}

inline IndexSummary summarizeIndex(const fs::path& root) {
    const fs::path resolved = fs::canonical(root);
    if (!fs::is_directory(resolved)) {
        throw std::invalid_argument("index root must be a directory");
    }
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(resolved)) {
        if (entry.is_symlink()) {
            throw std::invalid_argument("serialized index must not contain symlinks");
        }
        if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end(), [&](const auto& left, const auto& right) {
        return fs::relative(left, resolved).generic_string()
             < fs::relative(right, resolved).generic_string();
    });

    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
        EVP_MD_CTX_new(), EVP_MD_CTX_free
    );
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("cannot initialize SHA-256 digest");
    }
    IndexSummary result;
    std::array<char, 1024 * 1024> buffer{};
    for (const fs::path& file : files) {
        const std::string relative = fs::relative(file, resolved).generic_string();
        if (EVP_DigestUpdate(context.get(), relative.data(), relative.size()) != 1) {
            throw std::runtime_error("cannot hash index path");
        }
        const unsigned char separator = 0;
        if (EVP_DigestUpdate(context.get(), &separator, sizeof(separator)) != 1) {
            throw std::runtime_error("cannot hash index separator");
        }
        std::ifstream input(file, std::ios::binary);
        if (!input) throw std::runtime_error("cannot read serialized index file");
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            if (count > 0) {
                if (EVP_DigestUpdate(
                        context.get(), buffer.data(), static_cast<std::size_t>(count)
                    ) != 1) {
                    throw std::runtime_error("cannot hash serialized index file");
                }
                result.serialized_bytes += static_cast<std::uint64_t>(count);
            }
        }
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &digest_size) != 1) {
        throw std::runtime_error("cannot finalize index SHA-256");
    }
    result.index_sha256 = hexDigest(digest.data(), digest_size);
    return result;
}

inline bool filesEqual(const fs::path& left, const fs::path& right) {
    if (fs::file_size(left) != fs::file_size(right)) return false;
    std::ifstream a(left, std::ios::binary);
    std::ifstream b(right, std::ios::binary);
    if (!a || !b) throw std::runtime_error("cannot open save/reload signature");
    std::array<char, 64 * 1024> left_buffer{};
    std::array<char, 64 * 1024> right_buffer{};
    while (a && b) {
        a.read(left_buffer.data(), static_cast<std::streamsize>(left_buffer.size()));
        b.read(right_buffer.data(), static_cast<std::streamsize>(right_buffer.size()));
        if (a.gcount() != b.gcount()) return false;
        if (!std::equal(
                left_buffer.begin(), left_buffer.begin() + a.gcount(),
                right_buffer.begin())) {
            return false;
        }
    }
    return true;
}

}  // namespace indexprobe
