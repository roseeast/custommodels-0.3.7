#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

#include <custommodel/assets.hpp>

namespace custommodel::server {

enum class RegistryResult : std::uint8_t {
    Added,
    Removed,
    Cleared,
    NotFound,
    InvalidAsset,
    DuplicateAssetId,
    DuplicateLogicalName,
    AssetLimitReached,
    TotalSizeExceeded,
};

class AssetRegistry final {
public:
    RegistryResult RegisterAsset(assets::AssetDescription asset);
    RegistryResult RemoveAsset(std::uint32_t assetId);
    RegistryResult ClearAssets() noexcept;

    assets::Manifest BuildManifest() const;
    const assets::AssetDescription* Find(std::uint32_t assetId) const noexcept;
    std::uint64_t Revision() const noexcept;
    std::size_t Size() const noexcept;

private:
    void AdvanceRevision() noexcept;

    std::uint64_t revision_{1};
    std::uint64_t totalSize_{};
    std::unordered_map<std::uint32_t, assets::AssetDescription> assets_;
    std::unordered_map<std::string, std::uint32_t> logicalNames_;
};

struct ManifestConfigResult {
    std::size_t loadedAssets{};
    std::size_t errorLine{};
    std::string error;

    explicit operator bool() const noexcept {
        return error.empty();
    }
};

ManifestConfigResult LoadManifestConfig(
    const std::filesystem::path& path,
    AssetRegistry& registry
);
const char* ToString(RegistryResult result) noexcept;

}
