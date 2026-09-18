#include <custommodel/assets/cache.hpp>
#include <custommodel/assets/manager.hpp>
#include <custommodel/server/asset_publication.hpp>
#include <custommodel/server/asset_registry.hpp>
#include <custommodel/sha256.hpp>
#include <custommodel/transport_frame.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::filesystem::path TemporaryRoot(std::string_view name) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("custommodel-" + std::string{name} + "-" + std::to_string(suffix));
}

void WriteFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
}

custommodel::assets::AssetDescription MakeAsset(
    std::uint32_t id,
    std::string logicalName,
    std::string url,
    const std::vector<std::uint8_t>& payload,
    custommodel::assets::AssetType type = custommodel::assets::AssetType::Dff
) {
    return {
        id,
        type,
        std::move(logicalName),
        std::move(url),
        payload.size(),
        custommodel::assets::Sha256({payload.data(), payload.size()}),
    };
}

class FakeFetchBackend final : public custommodel::client_assets::FetchBackend {
public:
    enum class Mode {
        Exact,
        TooLarge,
        TooSmall,
        Timeout,
        NetworkFailure,
    };

    struct Response {
        Mode mode{Mode::Exact};
        std::vector<std::uint8_t> payload;
    };

    custommodel::client_assets::FetchResult Fetch(
        std::string_view url,
        const std::filesystem::path& destination,
        std::uint64_t expectedSize,
        const custommodel::client_assets::FetchOptions&
    ) noexcept override {
        try {
            Response response;
            {
                std::lock_guard<std::mutex> lock(mutex);
                ++fetchCount;
                const auto found = responses.find(std::string{url});
                if (found == responses.end()) {
                    return {custommodel::client_assets::FetchError::NetworkFailure, 0};
                }
                response = found->second;
            }
            if (response.mode == Mode::Timeout) {
                return {custommodel::client_assets::FetchError::Timeout, 0};
            }
            if (response.mode == Mode::NetworkFailure) {
                return {custommodel::client_assets::FetchError::NetworkFailure, 0};
            }
            if (response.mode == Mode::TooLarge) {
                response.payload.resize(static_cast<std::size_t>(expectedSize + 1), 0xEE);
            } else if (response.mode == Mode::TooSmall && !response.payload.empty()) {
                response.payload.pop_back();
            }
            WriteFile(destination, response.payload);
            return {
                response.payload.size() == expectedSize
                    ? custommodel::client_assets::FetchError::None
                    : custommodel::client_assets::FetchError::SizeMismatch,
                response.payload.size(),
            };
        } catch (...) {
            return {custommodel::client_assets::FetchError::DiskFailure, 0};
        }
    }

    std::mutex mutex;
    std::unordered_map<std::string, Response> responses;
    std::atomic<std::size_t> fetchCount{};
};

bool WaitForState(
    custommodel::client_assets::ClientAssetManager& manager,
    std::uint32_t assetId,
    custommodel::client_assets::AssetState expected
) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (manager.GetState(assetId) == expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

void DeliverManifest(
    custommodel::client_assets::ClientAssetManager& manager,
    std::uint64_t revision,
    const std::vector<custommodel::assets::AssetDescription>& assets
) {
    std::uint64_t total{};
    for (const auto& asset : assets) {
        total += asset.fileSize;
    }
    Check(
        manager.HandleMessage(custommodel::protocol::ManifestBegin{
            revision,
            static_cast<std::uint32_t>(assets.size()),
            total,
        }) == custommodel::client_assets::ManifestInputResult::Accepted,
        "manifest begin should be accepted"
    );
    for (std::size_t index = 0; index < assets.size(); ++index) {
        Check(
            manager.HandleMessage(custommodel::protocol::ManifestAsset{
                revision,
                static_cast<std::uint32_t>(index),
                assets[index],
            }) == custommodel::client_assets::ManifestInputResult::Accepted,
            "manifest asset should be accepted"
        );
    }
    Check(
        manager.HandleMessage(custommodel::protocol::ManifestEnd{
            revision,
            static_cast<std::uint32_t>(assets.size()),
            total,
        }) == custommodel::client_assets::ManifestInputResult::Complete,
        "manifest end should complete the stream"
    );
}

class FakeServerTransport final : public custommodel::server::ServerTransport {
public:
    bool Initialize(custommodel::server::IncomingHandler, void*) noexcept override {
        return true;
    }
    void Shutdown() noexcept override {}
    bool Send(
        std::uint32_t playerId,
        custommodel::protocol::ByteView bytes
    ) noexcept override {
        players.push_back(playerId);
        frames.emplace_back(bytes.data, bytes.data + bytes.size);
        return sendSucceeds;
    }
    bool DispatchIncoming(
        std::uint32_t,
        custommodel::protocol::ByteView
    ) noexcept override {
        return false;
    }
    bool IsAvailable() const noexcept override {
        return true;
    }

    bool sendSucceeds{true};
    std::vector<std::uint32_t> players;
    std::vector<std::vector<std::uint8_t>> frames;
};

void TestSha256() {
    const std::string value{"abc"};
    const auto digest = custommodel::assets::Sha256({
        reinterpret_cast<const std::uint8_t*>(value.data()),
        value.size(),
    });
    Check(
        custommodel::assets::Sha256Hex(digest) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 should match the standard abc vector"
    );
    Check(
        custommodel::assets::ParseSha256Hex(custommodel::assets::Sha256Hex(digest)) == digest,
        "lowercase SHA-256 text should round trip"
    );
    Check(
        !custommodel::assets::ParseSha256Hex(
            "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"
        ),
        "configuration SHA-256 should reject uppercase text"
    );
}

void TestManifestReceiver() {
    using namespace custommodel;
    const std::vector<std::uint8_t> payload{1, 2, 3, 4};
    const auto first = MakeAsset(1, "models/first", "https://example.test/first.dff", payload);
    const auto duplicateId = MakeAsset(1, "models/second", "https://example.test/second.dff", payload);

    client_assets::ManifestReceiver receiver;
    Check(
        receiver.Handle(protocol::ManifestBegin{
            0,
            1,
            first.fileSize,
        }) == client_assets::ManifestInputResult::InvalidManifest,
        "a zero manifest revision should fail closed"
    );
    receiver.Reset();
    Check(
        receiver.Handle(protocol::ManifestAsset{1, 0, first}) ==
            client_assets::ManifestInputResult::InvalidOrder,
        "manifest assets before Begin should be rejected"
    );
    receiver.Reset();
    Check(receiver.Handle(protocol::ManifestBegin{3, 1, first.fileSize}) ==
        client_assets::ManifestInputResult::Accepted, "valid Begin should start reception");
    Check(receiver.Handle(protocol::ManifestAsset{3, 0, first}) ==
        client_assets::ManifestInputResult::Accepted, "valid ordered asset should be accepted");
    Check(receiver.Handle(protocol::ManifestEnd{3, 1, first.fileSize}) ==
        client_assets::ManifestInputResult::Complete, "matching End should complete reception");

    receiver.Reset();
    receiver.Handle(protocol::ManifestBegin{4, 2, first.fileSize * 2});
    receiver.Handle(protocol::ManifestAsset{4, 0, first});
    Check(
        receiver.Handle(protocol::ManifestAsset{4, 1, duplicateId}) ==
            client_assets::ManifestInputResult::InvalidManifest,
        "duplicate asset IDs should invalidate a manifest"
    );

    receiver.Reset();
    auto duplicateName = MakeAsset(
        2,
        first.logicalName,
        "https://example.test/duplicate-name.dff",
        payload
    );
    receiver.Handle(protocol::ManifestBegin{6, 2, first.fileSize * 2});
    receiver.Handle(protocol::ManifestAsset{6, 0, first});
    Check(
        receiver.Handle(protocol::ManifestAsset{6, 1, duplicateName}) ==
            client_assets::ManifestInputResult::InvalidManifest,
        "duplicate logical names should invalidate a manifest"
    );

    receiver.Reset();
    receiver.Handle(protocol::ManifestBegin{5, 1, first.fileSize});
    Check(
        receiver.Handle(protocol::ManifestAsset{5, 1, first}) ==
            client_assets::ManifestInputResult::InvalidOrder,
        "out-of-order manifest indexes should be rejected"
    );
}

void TestRegistry() {
    using namespace custommodel;
    server::AssetRegistry registry;
    const std::vector<std::uint8_t> bytes{5, 6, 7};
    const auto third = MakeAsset(3, "models/third", "http://example.test/third.dff", bytes);
    const auto first = MakeAsset(1, "models/first", "https://example.test/first.dff", bytes);
    Check(registry.RegisterAsset(third) == server::RegistryResult::Added,
        "registry should accept a valid asset");
    const auto revision = registry.Revision();
    Check(registry.RegisterAsset(first) == server::RegistryResult::Added,
        "registry should accept another valid asset");
    Check(registry.Revision() > revision, "registry changes should advance revision");
    Check(registry.RegisterAsset(first) == server::RegistryResult::DuplicateAssetId,
        "duplicate asset IDs should be rejected");
    auto duplicateName = MakeAsset(2, first.logicalName, "https://example.test/two.dff", bytes);
    Check(registry.RegisterAsset(duplicateName) == server::RegistryResult::DuplicateLogicalName,
        "duplicate logical names should be rejected");
    const auto manifest = registry.BuildManifest();
    Check(manifest.assets.size() == 2 && manifest.assets[0].assetId == 1 &&
        manifest.assets[1].assetId == 3, "manifest ordering should be deterministic by asset ID");
    Check(registry.RemoveAsset(3) == server::RegistryResult::Removed,
        "registered assets should be removable");
    Check(registry.ClearAssets() == server::RegistryResult::Cleared && registry.Size() == 0,
        "registry clear should remove all assets");

    server::AssetRegistry limited;
    const std::vector<std::uint8_t> one{1};
    for (std::size_t index = 0; index < assets::kMaximumAssetsPerManifest; ++index) {
        const auto asset = MakeAsset(
            static_cast<std::uint32_t>(index + 1),
            "asset_" + std::to_string(index),
            "https://example.test/" + std::to_string(index),
            one
        );
        Check(limited.RegisterAsset(asset) == server::RegistryResult::Added,
            "registry should accept entries through its documented limit");
    }
    Check(limited.RegisterAsset(MakeAsset(999, "overflow", "https://example.test/x", one)) ==
        server::RegistryResult::AssetLimitReached, "registry should enforce its asset limit");

    server::AssetRegistry sizeLimited;
    for (std::uint32_t index = 0; index < 8; ++index) {
        auto large = MakeAsset(
            index + 1,
            "large_" + std::to_string(index),
            "https://example.test/large/" + std::to_string(index),
            one
        );
        large.fileSize = assets::kMaximumIndividualFileSize;
        Check(sizeLimited.RegisterAsset(large) == server::RegistryResult::Added,
            "registry should accept files through the total download limit");
    }
    auto tooMuch = MakeAsset(99, "too_much", "https://example.test/too-much", one);
    tooMuch.fileSize = assets::kMaximumIndividualFileSize;
    Check(sizeLimited.RegisterAsset(tooMuch) == server::RegistryResult::TotalSizeExceeded,
        "registry should enforce total announced download size");
}

void TestManifestConfiguration() {
    using namespace custommodel;
    const auto root = TemporaryRoot("manifest-config");
    const auto path = root / "custommodel-assets.cfg";
    std::filesystem::create_directories(root);
    {
        std::ofstream stream(path);
        stream << "1 DFF dev/test https://example.test/test.dff 3 "
            "039058c6f2c0cb492c533b0a4d14ef77cc0f78abccced5287d84a1a2011cfb81\n";
    }
    server::AssetRegistry registry;
    const auto loaded = server::LoadManifestConfig(path, registry);
    Check(loaded && loaded.loadedAssets == 1 && registry.Size() == 1,
        "valid manifest configuration should populate the registry");

    {
        std::ofstream stream(path, std::ios::trunc);
        stream << "1 DFF dev/test file:///tmp/test.dff 3 "
            "039058c6f2c0cb492c533b0a4d14ef77cc0f78abccced5287d84a1a2011cfb81\n";
    }
    const auto invalid = server::LoadManifestConfig(path, registry);
    Check(!invalid && invalid.errorLine == 1 && registry.Size() == 1,
        "invalid configuration should not partially replace the active registry");
    std::filesystem::remove_all(root);
}

void TestCache() {
    using namespace custommodel;
    const auto root = TemporaryRoot("cache");
    client_assets::AssetCache cache{root};
    Check(cache.Initialize(), "cache directories should initialize");
    const std::vector<std::uint8_t> payload{9, 8, 7, 6};
    const auto asset = MakeAsset(
        8,
        "models/cache_test",
        "https://example.test/cache.dff",
        payload
    );
    Check(cache.Lookup(asset) == client_assets::CacheLookup::Miss, "absent cache should miss");
    const auto cachePath = cache.PathFor(asset);
    Check(cachePath.string().find("cache_test") == std::string::npos &&
        cachePath.string().find("..") == std::string::npos,
        "cache paths should derive only from hashes and fixed extensions");
    WriteFile(cachePath, payload);
    Check(cache.Lookup(asset) == client_assets::CacheLookup::Hit,
        "matching size and SHA-256 should produce a cache hit");
    WriteFile(cachePath, {1});
    Check(cache.Lookup(asset) == client_assets::CacheLookup::Miss &&
        !std::filesystem::exists(cachePath), "wrong-size cache entries should be removed");
    WriteFile(cachePath, {1, 1, 1, 1});
    Check(cache.Lookup(asset) == client_assets::CacheLookup::Miss,
        "wrong-hash cache entries should be removed");
    std::filesystem::remove_all(root);
}

void TestDownloaderSuccessAndReuse() {
    using namespace custommodel;
    const auto root = TemporaryRoot("download-success");
    const std::vector<std::uint8_t> payload{10, 20, 30, 40, 50};
    auto backend = std::make_unique<FakeFetchBackend>();
    auto* fake = backend.get();
    const std::string url{"https://example.test/shared.dff"};
    fake->responses[url] = {FakeFetchBackend::Mode::Exact, payload};
    client_assets::ClientAssetManager manager{root, std::move(backend)};
    Check(manager.Initialize(), "asset manager should start one worker");
    const auto first = MakeAsset(11, "models/first", url, payload);
    const auto second = MakeAsset(12, "models/second", url, payload);
    DeliverManifest(manager, 20, {first, second});
    Check(WaitForState(manager, 11, client_assets::AssetState::Ready),
        "downloaded asset should become Ready");
    Check(WaitForState(manager, 12, client_assets::AssetState::Ready),
        "same-hash asset should reuse the cache");
    Check(fake->fetchCount.load() == 1, "same SHA-256 should not download twice");
    manager.Shutdown();
    std::filesystem::remove_all(root);
}

void TestDownloaderFailuresAndReplacement() {
    using namespace custommodel;
    const std::vector<std::uint8_t> payload{1, 3, 3, 7};
    struct Scenario {
        std::string name;
        FakeFetchBackend::Mode mode;
        std::vector<std::uint8_t> response;
        protocol::AssetErrorReason expected;
    };
    const Scenario scenarios[]{
        {"too-large", FakeFetchBackend::Mode::TooLarge, payload, protocol::AssetErrorReason::SizeMismatch},
        {"too-small", FakeFetchBackend::Mode::TooSmall, payload, protocol::AssetErrorReason::SizeMismatch},
        {"timeout", FakeFetchBackend::Mode::Timeout, payload, protocol::AssetErrorReason::Timeout},
        {"network", FakeFetchBackend::Mode::NetworkFailure, payload, protocol::AssetErrorReason::NetworkFailure},
        {"hash", FakeFetchBackend::Mode::Exact, {9, 9, 9, 9}, protocol::AssetErrorReason::HashMismatch},
    };

    std::uint32_t id = 100;
    for (const auto& scenario : scenarios) {
        const auto root = TemporaryRoot(scenario.name);
        auto backend = std::make_unique<FakeFetchBackend>();
        const std::string url = "https://example.test/" + scenario.name;
        backend->responses[url] = {scenario.mode, scenario.response};
        client_assets::ClientAssetManager manager{root, std::move(backend)};
        Check(manager.Initialize(), "failure-test manager should initialize");
        const auto asset = MakeAsset(id++, "models/" + scenario.name, url, payload);
        DeliverManifest(manager, id, {asset});
        Check(WaitForState(manager, asset.assetId, client_assets::AssetState::Failed),
            "invalid download should become Failed");
        bool foundReason{};
        for (const auto& event : manager.DrainEvents()) {
            if (event.kind == client_assets::ClientAssetEventKind::Failed &&
                event.error == scenario.expected) {
                foundReason = true;
            }
        }
        Check(foundReason, "download failure should map to the bounded protocol reason");
        const client_assets::AssetCache cache{root};
        Check(!std::filesystem::exists(cache.TemporaryPathFor(asset)),
            "failed downloads should remove temporary files");
        manager.Shutdown();
        std::filesystem::remove_all(root);
    }

    const auto root = TemporaryRoot("corrupt-replace");
    auto backend = std::make_unique<FakeFetchBackend>();
    const std::string url{"https://example.test/replace.dff"};
    backend->responses[url] = {FakeFetchBackend::Mode::Exact, payload};
    const auto asset = MakeAsset(500, "models/replace", url, payload);
    client_assets::AssetCache cache{root};
    cache.Initialize();
    WriteFile(cache.PathFor(asset), {0, 0, 0, 0});
    client_assets::ClientAssetManager manager{root, std::move(backend)};
    Check(manager.Initialize(), "replacement manager should initialize");
    DeliverManifest(manager, 501, {asset});
    Check(WaitForState(manager, asset.assetId, client_assets::AssetState::Ready),
        "corrupted cache should be replaced by a verified download");
    Check(cache.Lookup(asset) == client_assets::CacheLookup::Hit,
        "replacement cache entry should verify");
    manager.Shutdown();
    std::filesystem::remove_all(root);
}

void TestServerPublication() {
    using namespace custommodel;
    const std::vector<std::uint8_t> bytes{4, 2};
    server::AssetRegistry registry;
    const auto asset = MakeAsset(77, "models/server", "https://example.test/server.dff", bytes);
    registry.RegisterAsset(asset);
    FakeServerTransport transport;
    server::AssetPublication publication{registry, transport};
    publication.OnPlayerConnected(9);
    Check(
        publication.HandleIncoming(9, protocol::AssetReady{registry.Revision(), asset.assetId}) ==
            server::AssetDispatchResult::InvalidOrder,
        "asset reports before manifest completion should be rejected"
    );
    Check(publication.OnPlayerCompatible(9) == server::ManifestSendResult::Sent,
        "compatible players should receive a manifest");
    Check(transport.frames.size() == 3, "one asset should stream as Begin, Asset, End");
    const auto begin = protocol::DecodePacket(transport_frame::Inspect(transport.frames[0]).protocolBytes);
    const auto item = protocol::DecodePacket(transport_frame::Inspect(transport.frames[1]).protocolBytes);
    const auto end = protocol::DecodePacket(transport_frame::Inspect(transport.frames[2]).protocolBytes);
    Check(begin.packet.has_value() && std::holds_alternative<protocol::ManifestBegin>(begin.packet->message) &&
        item.packet.has_value() && std::holds_alternative<protocol::ManifestAsset>(item.packet->message) &&
        end.packet.has_value() && std::holds_alternative<protocol::ManifestEnd>(end.packet->message),
        "server publication should preserve streaming manifest order");
    Check(
        publication.HandleIncoming(9, protocol::AssetReady{registry.Revision(), asset.assetId}) ==
            server::AssetDispatchResult::ReadyRecorded,
        "valid AssetReady should be recorded"
    );
    Check(publication.GetState(9).state == server::AssetSessionState::AssetsReady,
        "all ready reports should complete the player asset state");
    Check(
        publication.HandleIncoming(9, protocol::AssetError{
            registry.Revision(),
            asset.assetId,
            protocol::AssetErrorReason::HashMismatch,
        }) == server::AssetDispatchResult::ErrorRecorded,
        "valid AssetError should be recorded"
    );
    publication.OnPlayerDisconnected(9);
    Check(publication.GetState(9).state == server::AssetSessionState::Unknown,
        "disconnect should erase server asset readiness");
    publication.OnPlayerConnected(9);
    Check(publication.GetState(9).state == server::AssetSessionState::HandshakePending,
        "reconnect should create a clean asset session");
}

void TestEndToEndAssetRoundTrip() {
    using namespace custommodel;
    const auto root = TemporaryRoot("round-trip");
    const std::vector<std::uint8_t> payload{0x44, 0x46, 0x46, 0x00, 0x01};
    const std::string url{"https://example.test/round-trip.dff"};
    const auto asset = MakeAsset(901, "models/round_trip", url, payload);

    server::AssetRegistry registry;
    Check(registry.RegisterAsset(asset) == server::RegistryResult::Added,
        "round-trip registry should accept the test asset");
    FakeServerTransport serverTransport;
    server::AssetPublication publication{registry, serverTransport};
    publication.OnPlayerConnected(5);
    Check(publication.OnPlayerCompatible(5) == server::ManifestSendResult::Sent,
        "round-trip server should publish the manifest after compatibility");

    auto backend = std::make_unique<FakeFetchBackend>();
    backend->responses[url] = {FakeFetchBackend::Mode::Exact, payload};
    client_assets::ClientAssetManager client{root, std::move(backend)};
    Check(client.Initialize(), "round-trip client asset manager should initialize");
    for (const auto& frame : serverTransport.frames) {
        const auto inspected = transport_frame::Inspect(frame);
        const auto decoded = protocol::DecodePacket(inspected.protocolBytes);
        Check(decoded.packet.has_value(), "published manifest packets should decode");
        if (decoded.packet.has_value()) {
            client.HandleMessage(decoded.packet->message);
        }
    }
    Check(WaitForState(client, asset.assetId, client_assets::AssetState::Ready),
        "round-trip client should verify and cache the asset");

    bool reported{};
    for (const auto& event : client.DrainEvents()) {
        if (event.kind == client_assets::ClientAssetEventKind::Ready) {
            reported = publication.HandleIncoming(5, protocol::AssetReady{
                event.manifestRevision,
                event.assetId,
            }) == server::AssetDispatchResult::ReadyRecorded;
        }
    }
    Check(reported, "round-trip client AssetReady should reach server state");
    Check(publication.GetState(5).state == server::AssetSessionState::AssetsReady,
        "round-trip server should finish in AssetsReady");
    client.Shutdown();
    std::filesystem::remove_all(root);
}

}

int main() {
    TestSha256();
    TestManifestReceiver();
    TestRegistry();
    TestManifestConfiguration();
    TestCache();
    TestDownloaderSuccessAndReuse();
    TestDownloaderFailuresAndReplacement();
    TestServerPublication();
    TestEndToEndAssetRoundTrip();

    if (failures != 0) {
        std::cerr << failures << " asset assertion(s) failed\n";
        return 1;
    }
    std::cout << "Asset tests passed\n";
    return 0;
}
