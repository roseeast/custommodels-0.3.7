#include <custommodel/server/asset_registry.hpp>

#include <custommodel/sha256.hpp>

#include <fstream>
#include <sstream>

namespace custommodel::server {
namespace {

std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

}

ManifestConfigResult LoadManifestConfig(
    const std::filesystem::path& path,
    AssetRegistry& registry
) {
    std::ifstream stream(path);
    if (!stream) {
        return {0, 0, "could not open manifest configuration"};
    }

    AssetRegistry parsed;
    std::string line;
    std::size_t lineNumber{};
    while (std::getline(stream, line)) {
        ++lineNumber;
        line = Trim(std::move(line));
        if (line.empty() || line.front() == '#') {
            continue;
        }

        std::uint32_t assetId{};
        std::string type;
        std::string logicalName;
        std::string url;
        std::uint64_t fileSize{};
        std::string sha256;
        std::string trailing;
        std::istringstream fields(line);
        if (!(fields >> assetId >> type >> logicalName >> url >> fileSize >> sha256) ||
            (fields >> trailing)) {
            return {0, lineNumber, "expected six whitespace-separated fields"};
        }

        const auto digest = assets::ParseSha256Hex(sha256);
        if (!digest.has_value()) {
            return {0, lineNumber, "SHA-256 must contain 64 lowercase hexadecimal characters"};
        }

        assets::AssetType assetType{};
        if (type == "DFF") {
            assetType = assets::AssetType::Dff;
        } else if (type == "TXD") {
            assetType = assets::AssetType::Txd;
        } else {
            return {0, lineNumber, "asset type must be DFF or TXD"};
        }

        const auto result = parsed.RegisterAsset({
            assetId,
            assetType,
            std::move(logicalName),
            std::move(url),
            fileSize,
            *digest,
        });
        if (result != RegistryResult::Added) {
            return {0, lineNumber, ToString(result)};
        }
    }
    if (!stream.eof()) {
        return {0, lineNumber, "manifest configuration read failed"};
    }

    registry = std::move(parsed);
    return {registry.Size(), 0, {}};
}

}
