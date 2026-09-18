#pragma once

#include <filesystem>

#include <custommodel/assets.hpp>

namespace custommodel::client_assets {

enum class CacheLookup : std::uint8_t {
    Hit,
    Miss,
    DiskFailure,
};

class AssetCache final {
public:
    explicit AssetCache(std::filesystem::path root);

    bool Initialize() noexcept;
    CacheLookup Lookup(const assets::AssetDescription& asset) noexcept;
    std::filesystem::path PathFor(const assets::AssetDescription& asset) const;
    std::filesystem::path TemporaryPathFor(const assets::AssetDescription& asset) const;
    bool CommitVerified(
        const assets::AssetDescription& asset,
        const std::filesystem::path& temporaryPath
    ) noexcept;
    void RemoveTemporary(const std::filesystem::path& temporaryPath) noexcept;

    const std::filesystem::path& Root() const noexcept;

private:
    std::filesystem::path root_;
};

}
