#include <custommodel/assets.hpp>

#include <algorithm>
#include <cctype>
#include <limits>

namespace custommodel::assets {
namespace {

bool IsAsciiIdentifierByte(unsigned char value) noexcept {
    return std::isalnum(value) != 0 || value == '_' || value == '-' ||
        value == '.' || value == '/';
}

bool HasInvalidPathSegment(std::string_view name) noexcept {
    std::size_t offset{};
    while (offset <= name.size()) {
        const auto separator = name.find('/', offset);
        const auto length = separator == std::string_view::npos
            ? name.size() - offset
            : separator - offset;
        const auto segment = name.substr(offset, length);
        if (segment.empty() || segment == "." || segment == "..") {
            return true;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        offset = separator + 1;
    }
    return false;
}

bool StartsWithCaseInsensitive(std::string_view value, std::string_view prefix) noexcept {
    if (value.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(value[index])) !=
            std::tolower(static_cast<unsigned char>(prefix[index]))) {
            return false;
        }
    }
    return true;
}

}

bool IsAssetTypeValid(AssetType type) noexcept {
    return type == AssetType::Dff || type == AssetType::Txd;
}

bool IsLogicalNameValid(std::string_view name) noexcept {
    if (name.empty() || name.size() > kMaximumLogicalNameLength ||
        name.front() == '/' || name.back() == '/') {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char value) {
        return IsAsciiIdentifierByte(static_cast<unsigned char>(value));
    }) && !HasInvalidPathSegment(name);
}

bool IsDownloadUrlValid(std::string_view url) noexcept {
    if (url.empty() || url.size() > kMaximumUrlLength) {
        return false;
    }
    if (!StartsWithCaseInsensitive(url, "http://") &&
        !StartsWithCaseInsensitive(url, "https://")) {
        return false;
    }

    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) {
        return false;
    }
    const auto authorityStart = schemeEnd + 3;
    const auto authorityEnd = url.find_first_of("/?#", authorityStart);
    const auto authority = url.substr(
        authorityStart,
        authorityEnd == std::string_view::npos
            ? url.size() - authorityStart
            : authorityEnd - authorityStart
    );
    if (authority.empty() || authority.find('@') != std::string_view::npos ||
        url.find('\\') != std::string_view::npos ||
        url.find('#') != std::string_view::npos) {
        return false;
    }

    return std::all_of(url.begin(), url.end(), [](char value) {
        const auto byte = static_cast<unsigned char>(value);
        return byte >= 0x21 && byte <= 0x7E;
    });
}

bool IsAssetDescriptionValid(const AssetDescription& asset) noexcept {
    return asset.assetId != 0 && IsAssetTypeValid(asset.type) &&
        IsLogicalNameValid(asset.logicalName) &&
        IsDownloadUrlValid(asset.downloadUrl) && asset.fileSize != 0 &&
        asset.fileSize <= kMaximumIndividualFileSize;
}

std::size_t SerializedAssetBytes(const AssetDescription& asset) noexcept {
    constexpr std::size_t fixedBytes = 8 + 4 + 4 + 2 + 2 + 2 + 8 + kSha256Size;
    if (asset.logicalName.size() > std::numeric_limits<std::size_t>::max() - fixedBytes ||
        asset.downloadUrl.size() >
            std::numeric_limits<std::size_t>::max() - fixedBytes - asset.logicalName.size()) {
        return std::numeric_limits<std::size_t>::max();
    }
    return fixedBytes + asset.logicalName.size() + asset.downloadUrl.size();
}

const char* ExtensionFor(AssetType type) noexcept {
    switch (type) {
    case AssetType::Dff:
        return ".dff";
    case AssetType::Txd:
        return ".txd";
    }
    return "";
}

const char* ToString(AssetType type) noexcept {
    switch (type) {
    case AssetType::Dff:
        return "DFF";
    case AssetType::Txd:
        return "TXD";
    }
    return "unknown";
}

}
