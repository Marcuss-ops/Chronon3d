#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/hash/hash_builder.hpp>

#include "sha256.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef CHRONON3D_HAS_OPENSSL_SHA256
#if __has_include(<openssl/evp.h>)
#include <openssl/evp.h>
#else
extern "C" {
struct evp_md_ctx_st;
struct evp_md_st;
using EVP_MD_CTX = evp_md_ctx_st;
using EVP_MD = evp_md_st;
EVP_MD_CTX* EVP_MD_CTX_new();
void EVP_MD_CTX_free(EVP_MD_CTX* ctx);
const EVP_MD* EVP_sha256();
int EVP_DigestInit_ex(EVP_MD_CTX* ctx, const EVP_MD* type, void* impl);
int EVP_DigestUpdate(EVP_MD_CTX* ctx, const void* data, std::size_t count);
int EVP_DigestFinal_ex(EVP_MD_CTX* ctx, unsigned char* md, unsigned int* size);
}
#endif
#endif

namespace chronon3d::assets {
namespace {

using detail::Sha256;

} // namespace

ContentDigest digest_from_u64(std::uint64_t value) noexcept {
    // splitmix64 — deterministic avalanche from 64→32 bytes
    ContentDigest digest{};
    for (size_t word = 0; word < 4; ++word) {
        const uint64_t w = core::hash::splitmix64(value + static_cast<uint64_t>(word) * 0x9e3779b97f4a7c15ULL);
        for (size_t b = 0; b < 8; ++b) {
            digest.bytes[word * 8 + b] = static_cast<std::byte>(
                static_cast<unsigned char>((w >> (b * 8)) & 0xFF));
        }
    }
    return digest;
}

std::string ContentDigest::hex() const {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : bytes)
        output << std::setw(2) << static_cast<unsigned>(std::to_integer<unsigned>(byte));
    return output.str();
}

ContentDigest sha256_string(std::string_view value) {
    Sha256 sha;
    sha.update(value.data(), value.size());
    return sha.finish();
}

std::optional<ContentDigest> sha256_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;

#ifdef CHRONON3D_HAS_OPENSSL_SHA256
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context) return std::nullopt;
    const auto cleanup = [&context]() { EVP_MD_CTX_free(context); };
    if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
        cleanup();
        return std::nullopt;
    }
    std::array<char, 1024U * 1024U> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count <= 0) break;
        if (EVP_DigestUpdate(context, buffer.data(), static_cast<std::size_t>(count)) != 1) {
            cleanup();
            return std::nullopt;
        }
    }
    if (input.bad()) {
        cleanup();
        return std::nullopt;
    }
    std::array<unsigned char, 64> digest_bytes{};
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(context, digest_bytes.data(), &digest_size) != 1 ||
        digest_size != 32U) {
        cleanup();
        return std::nullopt;
    }
    ContentDigest digest;
    for (std::size_t i = 0; i < digest.bytes.size(); ++i)
        digest.bytes[i] = static_cast<std::byte>(digest_bytes[i]);
    cleanup();
    return digest;
#else
    Sha256 sha;
    std::array<char, 64U * 1024U> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count <= 0) break;
        sha.update(buffer.data(), static_cast<std::size_t>(count));
    }
    if (input.bad()) return std::nullopt;
    return sha.finish();
#endif
}

} // namespace chronon3d::assets
