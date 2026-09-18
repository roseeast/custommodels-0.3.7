#include <custommodel/sha256.hpp>

#include <picosha2/picosha2.h>

#include <array>
#include <fstream>

namespace custommodel::assets {
namespace {

int HexValue(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

}

Sha256Digest Sha256(protocol::ByteView bytes) noexcept {
    Sha256Digest digest{};
    try {
        static constexpr std::uint8_t empty{};
        if (bytes.data == nullptr && bytes.size != 0) {
            return digest;
        }
        const auto* begin = bytes.data == nullptr ? &empty : bytes.data;
        picosha2::hash256(
            begin,
            begin + bytes.size,
            digest.begin(),
            digest.end()
        );
    } catch (...) {
        digest.fill(0);
    }
    return digest;
}

std::optional<Sha256Digest> Sha256File(const std::filesystem::path& path) noexcept {
    try {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            return std::nullopt;
        }

        picosha2::hash256_one_by_one hasher;
        std::array<unsigned char, 64 * 1024> buffer{};
        while (stream) {
            stream.read(
                reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(buffer.size())
            );
            const auto count = stream.gcount();
            if (count > 0) {
                hasher.process(buffer.begin(), buffer.begin() + count);
            }
        }
        if (!stream.eof()) {
            return std::nullopt;
        }

        hasher.finish();
        Sha256Digest digest{};
        hasher.get_hash_bytes(digest.begin(), digest.end());
        return digest;
    } catch (...) {
        return std::nullopt;
    }
}

std::string Sha256Hex(const Sha256Digest& digest) {
    return picosha2::bytes_to_hex_string(digest.begin(), digest.end());
}

std::optional<Sha256Digest> ParseSha256Hex(std::string_view hex) noexcept {
    if (hex.size() != kSha256Size * 2) {
        return std::nullopt;
    }

    Sha256Digest digest{};
    for (std::size_t index = 0; index < digest.size(); ++index) {
        const auto high = HexValue(hex[index * 2]);
        const auto low = HexValue(hex[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        digest[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return digest;
}

}
