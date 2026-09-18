#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace custommodel::assets {

inline constexpr std::size_t kSha256Size = 32;
inline constexpr std::size_t kMaximumAssetsPerManifest = 256;
inline constexpr std::size_t kMaximumLogicalNameLength = 96;
inline constexpr std::size_t kMaximumUrlLength = 2048;
inline constexpr std::uint64_t kMaximumIndividualFileSize = 64ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kMaximumTotalDownloadSize = 512ULL * 1024ULL * 1024ULL;
inline constexpr std::size_t kMaximumManifestBytes = 1024ULL * 1024ULL;

using Sha256Digest = std::array<std::uint8_t, kSha256Size>;

enum class AssetType : std::uint16_t {
    Dff = 1,
    Txd = 2,
};

struct AssetDescription {
    std::uint32_t assetId{};
    AssetType type{AssetType::Dff};
    std::string logicalName;
    std::string downloadUrl;
    std::uint64_t fileSize{};
    Sha256Digest sha256{};
};

struct Manifest {
    std::uint64_t revision{};
    std::vector<AssetDescription> assets;
    std::uint64_t totalDownloadSize{};
};

bool IsAssetTypeValid(AssetType type) noexcept;
bool IsLogicalNameValid(std::string_view name) noexcept;
bool IsDownloadUrlValid(std::string_view url) noexcept;
bool IsAssetDescriptionValid(const AssetDescription& asset) noexcept;
std::size_t SerializedAssetBytes(const AssetDescription& asset) noexcept;
const char* ExtensionFor(AssetType type) noexcept;
const char* ToString(AssetType type) noexcept;

}
