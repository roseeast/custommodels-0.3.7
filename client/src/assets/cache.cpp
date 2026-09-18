#include <custommodel/assets/cache.hpp>

#include <custommodel/sha256.hpp>

#include <system_error>

namespace custommodel::client_assets {

AssetCache::AssetCache(std::filesystem::path root) : root_(std::move(root)) {}

bool AssetCache::Initialize() noexcept {
    try {
        std::error_code error;
        std::filesystem::create_directories(root_ / "sha256", error);
        if (error) {
            return false;
        }
        std::filesystem::create_directories(root_ / "tmp", error);
        return !error;
    } catch (...) {
        return false;
    }
}

CacheLookup AssetCache::Lookup(const assets::AssetDescription& asset) noexcept {
    try {
        const auto path = PathFor(asset);
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            return error ? CacheLookup::DiskFailure : CacheLookup::Miss;
        }
        if (!std::filesystem::is_regular_file(path, error) || error) {
            std::filesystem::remove(path, error);
            return error ? CacheLookup::DiskFailure : CacheLookup::Miss;
        }
        const auto size = std::filesystem::file_size(path, error);
        const auto digest = !error ? assets::Sha256File(path) : std::nullopt;
        if (!error && size == asset.fileSize && digest.has_value() &&
            *digest == asset.sha256) {
            return CacheLookup::Hit;
        }

        error.clear();
        std::filesystem::remove(path, error);
        return error ? CacheLookup::DiskFailure : CacheLookup::Miss;
    } catch (...) {
        return CacheLookup::DiskFailure;
    }
}

std::filesystem::path AssetCache::PathFor(
    const assets::AssetDescription& asset
) const {
    const auto hash = assets::Sha256Hex(asset.sha256);
    return root_ / "sha256" / hash.substr(0, 2) /
        (hash + assets::ExtensionFor(asset.type));
}

std::filesystem::path AssetCache::TemporaryPathFor(
    const assets::AssetDescription& asset
) const {
    return root_ / "tmp" /
        (assets::Sha256Hex(asset.sha256) + assets::ExtensionFor(asset.type) + ".tmp");
}

bool AssetCache::CommitVerified(
    const assets::AssetDescription& asset,
    const std::filesystem::path& temporaryPath
) noexcept {
    try {
        const auto destination = PathFor(asset);
        std::error_code error;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) {
            return false;
        }

        if (std::filesystem::exists(destination, error)) {
            if (error) {
                return false;
            }
            if (Lookup(asset) == CacheLookup::Hit) {
                RemoveTemporary(temporaryPath);
                return true;
            }
        }

        error.clear();
        std::filesystem::rename(temporaryPath, destination, error);
        return !error;
    } catch (...) {
        return false;
    }
}

void AssetCache::RemoveTemporary(const std::filesystem::path& temporaryPath) noexcept {
    try {
        std::error_code error;
        std::filesystem::remove(temporaryPath, error);
    } catch (...) {
    }
}

const std::filesystem::path& AssetCache::Root() const noexcept {
    return root_;
}

}
