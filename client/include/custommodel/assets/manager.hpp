#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <custommodel/assets/cache.hpp>
#include <custommodel/assets/fetch.hpp>
#include <custommodel/protocol.hpp>

namespace custommodel::client_assets {

enum class AssetState : std::uint8_t {
    Unknown,
    Queued,
    Downloading,
    Verifying,
    Ready,
    Failed,
};

enum class ManifestInputResult : std::uint8_t {
    Ignored,
    Accepted,
    Complete,
    InvalidOrder,
    InvalidManifest,
};

enum class ClientAssetEventKind : std::uint8_t {
    ManifestBegin,
    ManifestComplete,
    CacheHit,
    Queued,
    Downloading,
    Verifying,
    Verified,
    Ready,
    Failed,
};

struct ClientAssetEvent {
    ClientAssetEventKind kind{ClientAssetEventKind::Failed};
    std::uint64_t manifestRevision{};
    std::uint32_t assetId{};
    std::uint32_t assetCount{};
    protocol::AssetErrorReason error{protocol::AssetErrorReason::InternalError};
};

class ManifestReceiver final {
public:
    ManifestInputResult Handle(const protocol::Message& message);
    void Reset() noexcept;
    const std::optional<assets::Manifest>& CompletedManifest() const noexcept;

private:
    enum class State : std::uint8_t {
        Idle,
        Receiving,
        Complete,
        Failed,
    };

    State state_{State::Idle};
    assets::Manifest manifest_{};
    std::uint32_t expectedCount_{};
    std::size_t manifestBytes_{};
    std::unordered_set<std::uint32_t> assetIds_;
    std::unordered_set<std::string> logicalNames_;
    std::optional<assets::Manifest> completed_;
};

class ClientAssetManager final {
public:
    ClientAssetManager(
        std::filesystem::path cacheRoot,
        std::unique_ptr<FetchBackend> fetchBackend
    );
    ~ClientAssetManager();

    ClientAssetManager(const ClientAssetManager&) = delete;
    ClientAssetManager& operator=(const ClientAssetManager&) = delete;

    bool Initialize();
    void Shutdown() noexcept;
    void RequestStop() noexcept;
    void ResetSession() noexcept;
    ManifestInputResult HandleMessage(const protocol::Message& message);
    std::vector<ClientAssetEvent> DrainEvents();
    AssetState GetState(std::uint32_t assetId) const noexcept;

private:
    struct WorkItem {
        std::uint64_t generation{};
        std::uint64_t manifestRevision{};
        assets::AssetDescription asset;
    };

    void QueueManifest(const assets::Manifest& manifest);
    void WorkerLoop() noexcept;
    void Process(WorkItem work) noexcept;
    void SetState(
        const WorkItem& work,
        AssetState state,
        ClientAssetEventKind event,
        protocol::AssetErrorReason error = protocol::AssetErrorReason::InternalError
    );
    void Fail(const WorkItem& work, protocol::AssetErrorReason reason) noexcept;
    bool IsCurrentGeneration(std::uint64_t generation) const noexcept;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    AssetCache cache_;
    std::unique_ptr<FetchBackend> fetchBackend_;
    FetchOptions fetchOptions_{};
    ManifestReceiver receiver_;
    std::deque<WorkItem> queue_;
    std::vector<ClientAssetEvent> events_;
    std::unordered_map<std::uint32_t, AssetState> states_;
    std::thread worker_;
    std::uint64_t generation_{1};
    bool initialized_{};
    bool stopping_{};
};

}
