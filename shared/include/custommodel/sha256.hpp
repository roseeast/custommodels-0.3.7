#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <custommodel/assets.hpp>
#include <custommodel/protocol.hpp>

namespace custommodel::assets {

Sha256Digest Sha256(protocol::ByteView bytes) noexcept;
std::optional<Sha256Digest> Sha256File(const std::filesystem::path& path) noexcept;
std::string Sha256Hex(const Sha256Digest& digest);
std::optional<Sha256Digest> ParseSha256Hex(std::string_view hex) noexcept;

}
