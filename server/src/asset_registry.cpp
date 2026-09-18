#include <custommodel/server/asset_registry.hpp>

#include <algorithm>
#include <limits>

namespace custommodel::server {

RegistryResult AssetRegistry::RegisterAsset(assets::AssetDescription asset) {
    if (!assets::IsAssetDescriptionValid(asset)) {
        return RegistryResult::InvalidAsset;
    }
    if (assets_.find(asset.assetId) != assets_.end()) {
        return RegistryResult::DuplicateAssetId;
    }
    if (logicalNames_.find(asset.logicalName) != logicalNames_.end()) {
        return RegistryResult::DuplicateLogicalName;
    }
    if (assets_.size() >= assets::kMaximumAssetsPerManifest) {
        return RegistryResult::AssetLimitReached;
    }
    if (asset.fileSize > assets::kMaximumTotalDownloadSize - totalSize_) {
        return RegistryResult::TotalSizeExceeded;
    }

    totalSize_ += asset.fileSize;
    logicalNames_.emplace(asset.logicalName, asset.assetId);
    assets_.emplace(asset.assetId, std::move(asset));
    AdvanceRevision();
    return RegistryResult::Added;
}

RegistryResult AssetRegistry::RemoveAsset(std::uint32_t assetId) {
    const auto found = assets_.find(assetId);
    if (found == assets_.end()) {
        return RegistryResult::NotFound;
    }
    totalSize_ -= found->second.fileSize;
    logicalNames_.erase(found->second.logicalName);
    assets_.erase(found);
    AdvanceRevision();
    return RegistryResult::Removed;
}

RegistryResult AssetRegistry::ClearAssets() noexcept {
    if (assets_.empty()) {
        return RegistryResult::Cleared;
    }
    assets_.clear();
    logicalNames_.clear();
    totalSize_ = 0;
    AdvanceRevision();
    return RegistryResult::Cleared;
}

assets::Manifest AssetRegistry::BuildManifest() const {
    assets::Manifest manifest{};
    manifest.revision = revision_;
    manifest.totalDownloadSize = totalSize_;
    manifest.assets.reserve(assets_.size());
    for (const auto& entry : assets_) {
        manifest.assets.push_back(entry.second);
    }
    std::sort(manifest.assets.begin(), manifest.assets.end(), [](const auto& left, const auto& right) {
        return left.assetId < right.assetId;
    });
    return manifest;
}

const assets::AssetDescription* AssetRegistry::Find(std::uint32_t assetId) const noexcept {
    const auto found = assets_.find(assetId);
    return found == assets_.end() ? nullptr : &found->second;
}

std::uint64_t AssetRegistry::Revision() const noexcept {
    return revision_;
}

std::size_t AssetRegistry::Size() const noexcept {
    return assets_.size();
}

void AssetRegistry::AdvanceRevision() noexcept {
    if (revision_ != std::numeric_limits<std::uint64_t>::max()) {
        ++revision_;
    }
}

const char* ToString(RegistryResult result) noexcept {
    switch (result) {
    case RegistryResult::Added:
        return "added";
    case RegistryResult::Removed:
        return "removed";
    case RegistryResult::Cleared:
        return "cleared";
    case RegistryResult::NotFound:
        return "not found";
    case RegistryResult::InvalidAsset:
        return "invalid asset";
    case RegistryResult::DuplicateAssetId:
        return "duplicate asset id";
    case RegistryResult::DuplicateLogicalName:
        return "duplicate logical name";
    case RegistryResult::AssetLimitReached:
        return "asset limit reached";
    case RegistryResult::TotalSizeExceeded:
        return "total size exceeded";
    }
    return "unknown registry result";
}

}
