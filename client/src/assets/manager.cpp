#include <custommodel/assets/manager.hpp>

#include <custommodel/sha256.hpp>

#include <filesystem>
#include <limits>
#include <variant>

namespace custommodel::client_assets {
namespace {

protocol::AssetErrorReason MapFetchError(FetchError error) noexcept {
    switch (error) {
    case FetchError::UnsupportedScheme:
        return protocol::AssetErrorReason::UnsupportedScheme;
    case FetchError::NetworkFailure:
        return protocol::AssetErrorReason::NetworkFailure;
    case FetchError::Timeout:
        return protocol::AssetErrorReason::Timeout;
    case FetchError::SizeMismatch:
        return protocol::AssetErrorReason::SizeMismatch;
    case FetchError::DiskFailure:
        return protocol::AssetErrorReason::DiskFailure;
    case FetchError::InternalError:
    case FetchError::None:
        return protocol::AssetErrorReason::InternalError;
    }
    return protocol::AssetErrorReason::InternalError;
}

}

ManifestInputResult ManifestReceiver::Handle(const protocol::Message& message) {
    if (const auto* begin = std::get_if<protocol::ManifestBegin>(&message)) {
        if (state_ != State::Idle) {
            state_ = State::Failed;
            return ManifestInputResult::InvalidOrder;
        }
        if (begin->manifestRevision == 0 ||
            begin->assetCount > assets::kMaximumAssetsPerManifest ||
            begin->totalDownloadSize > assets::kMaximumTotalDownloadSize ||
            (begin->assetCount == 0 && begin->totalDownloadSize != 0)) {
            state_ = State::Failed;
            return ManifestInputResult::InvalidManifest;
        }
        manifest_ = {};
        manifest_.revision = begin->manifestRevision;
        manifest_.totalDownloadSize = begin->totalDownloadSize;
        expectedCount_ = begin->assetCount;
        manifestBytes_ = 20;
        assetIds_.clear();
        logicalNames_.clear();
        completed_.reset();
        state_ = State::Receiving;
        return ManifestInputResult::Accepted;
    }

    if (const auto* item = std::get_if<protocol::ManifestAsset>(&message)) {
        if (state_ != State::Receiving ||
            item->manifestRevision != manifest_.revision ||
            item->assetIndex != manifest_.assets.size() ||
            manifest_.assets.size() >= expectedCount_) {
            state_ = State::Failed;
            return ManifestInputResult::InvalidOrder;
        }
        const auto bytes = assets::SerializedAssetBytes(item->asset);
        if (!assets::IsAssetDescriptionValid(item->asset) ||
            bytes > assets::kMaximumManifestBytes - manifestBytes_ ||
            !assetIds_.insert(item->asset.assetId).second ||
            !logicalNames_.insert(item->asset.logicalName).second) {
            state_ = State::Failed;
            return ManifestInputResult::InvalidManifest;
        }
        manifestBytes_ += bytes;
        manifest_.assets.push_back(item->asset);
        return ManifestInputResult::Accepted;
    }

    if (const auto* end = std::get_if<protocol::ManifestEnd>(&message)) {
        if (state_ != State::Receiving ||
            end->manifestRevision != manifest_.revision ||
            end->assetCount != expectedCount_ ||
            end->assetCount != manifest_.assets.size() ||
            end->totalDownloadSize != manifest_.totalDownloadSize) {
            state_ = State::Failed;
            return ManifestInputResult::InvalidOrder;
        }
        std::uint64_t total{};
        for (const auto& asset : manifest_.assets) {
            if (asset.fileSize > assets::kMaximumTotalDownloadSize - total) {
                state_ = State::Failed;
                return ManifestInputResult::InvalidManifest;
            }
            total += asset.fileSize;
        }
        if (total != manifest_.totalDownloadSize) {
            state_ = State::Failed;
            return ManifestInputResult::InvalidManifest;
        }
        manifestBytes_ += 20;
        if (manifestBytes_ > assets::kMaximumManifestBytes) {
            state_ = State::Failed;
            return ManifestInputResult::InvalidManifest;
        }
        completed_ = manifest_;
        state_ = State::Complete;
        return ManifestInputResult::Complete;
    }

    return ManifestInputResult::Ignored;
}

void ManifestReceiver::Reset() noexcept {
    state_ = State::Idle;
    manifest_ = {};
    expectedCount_ = 0;
    manifestBytes_ = 0;
    assetIds_.clear();
    logicalNames_.clear();
    completed_.reset();
}

const std::optional<assets::Manifest>& ManifestReceiver::CompletedManifest() const noexcept {
    return completed_;
}

ClientAssetManager::ClientAssetManager(
    std::filesystem::path cacheRoot,
    std::unique_ptr<FetchBackend> fetchBackend
) : cache_(std::move(cacheRoot)), fetchBackend_(std::move(fetchBackend)) {}

ClientAssetManager::~ClientAssetManager() {
    Shutdown();
}

bool ClientAssetManager::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_ || fetchBackend_ == nullptr || !cache_.Initialize()) {
        return false;
    }
    stopping_ = false;
    worker_ = std::thread(&ClientAssetManager::WorkerLoop, this);
    initialized_ = true;
    return true;
}

void ClientAssetManager::Shutdown() noexcept {
    RequestStop();
    try {
        if (worker_.joinable()) {
            worker_.join();
        }
    } catch (...) {
    }
    std::lock_guard<std::mutex> lock(mutex_);
    initialized_ = false;
}

void ClientAssetManager::RequestStop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        queue_.clear();
    }
    condition_.notify_all();
}

void ClientAssetManager::ResetSession() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    ++generation_;
    if (generation_ == 0) {
        generation_ = 1;
    }
    receiver_.Reset();
    queue_.clear();
    events_.clear();
    states_.clear();
}

ManifestInputResult ClientAssetManager::HandleMessage(const protocol::Message& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto result = receiver_.Handle(message);
    if (result == ManifestInputResult::Accepted) {
        if (const auto* begin = std::get_if<protocol::ManifestBegin>(&message)) {
            events_.push_back({
                ClientAssetEventKind::ManifestBegin,
                begin->manifestRevision,
                0,
                begin->assetCount,
                protocol::AssetErrorReason::InternalError,
            });
        }
    } else if (result == ManifestInputResult::Complete &&
               receiver_.CompletedManifest().has_value()) {
        const auto manifest = *receiver_.CompletedManifest();
        events_.push_back({
            ClientAssetEventKind::ManifestComplete,
            manifest.revision,
            0,
            static_cast<std::uint32_t>(manifest.assets.size()),
            protocol::AssetErrorReason::InternalError,
        });
        QueueManifest(manifest);
    }
    return result;
}

std::vector<ClientAssetEvent> ClientAssetManager::DrainEvents() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = std::move(events_);
    events_.clear();
    return result;
}

AssetState ClientAssetManager::GetState(std::uint32_t assetId) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = states_.find(assetId);
    return found == states_.end() ? AssetState::Unknown : found->second;
}

void ClientAssetManager::QueueManifest(const assets::Manifest& manifest) {
    for (const auto& asset : manifest.assets) {
        const auto found = states_.find(asset.assetId);
        if (found != states_.end() && found->second != AssetState::Failed) {
            continue;
        }
        states_[asset.assetId] = AssetState::Queued;
        queue_.push_back({generation_, manifest.revision, asset});
        events_.push_back({
            ClientAssetEventKind::Queued,
            manifest.revision,
            asset.assetId,
            0,
            protocol::AssetErrorReason::InternalError,
        });
    }
    condition_.notify_one();
}

void ClientAssetManager::WorkerLoop() noexcept {
    for (;;) {
        WorkItem work{};
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) {
                return;
            }
            work = std::move(queue_.front());
            queue_.pop_front();
        }
        Process(std::move(work));
    }
}

void ClientAssetManager::Process(WorkItem work) noexcept {
    try {
        if (!IsCurrentGeneration(work.generation)) {
            return;
        }

        const auto lookup = cache_.Lookup(work.asset);
        if (lookup == CacheLookup::Hit) {
            SetState(work, AssetState::Ready, ClientAssetEventKind::CacheHit);
            SetState(work, AssetState::Ready, ClientAssetEventKind::Ready);
            return;
        }
        if (lookup == CacheLookup::DiskFailure) {
            Fail(work, protocol::AssetErrorReason::DiskFailure);
            return;
        }

        SetState(work, AssetState::Downloading, ClientAssetEventKind::Downloading);
        const auto temporaryPath = cache_.TemporaryPathFor(work.asset);
        cache_.RemoveTemporary(temporaryPath);
        const auto fetched = fetchBackend_->Fetch(
            work.asset.downloadUrl,
            temporaryPath,
            work.asset.fileSize,
            fetchOptions_
        );
        if (!fetched) {
            cache_.RemoveTemporary(temporaryPath);
            Fail(work, MapFetchError(fetched.error));
            return;
        }
        if (fetched.bytesReceived != work.asset.fileSize) {
            cache_.RemoveTemporary(temporaryPath);
            Fail(work, protocol::AssetErrorReason::SizeMismatch);
            return;
        }

        SetState(work, AssetState::Verifying, ClientAssetEventKind::Verifying);
        std::error_code fileError;
        const auto fileSize = std::filesystem::file_size(temporaryPath, fileError);
        if (fileError || fileSize != work.asset.fileSize) {
            cache_.RemoveTemporary(temporaryPath);
            Fail(work, fileError
                ? protocol::AssetErrorReason::DiskFailure
                : protocol::AssetErrorReason::SizeMismatch);
            return;
        }
        const auto digest = assets::Sha256File(temporaryPath);
        if (!digest.has_value()) {
            cache_.RemoveTemporary(temporaryPath);
            Fail(work, protocol::AssetErrorReason::DiskFailure);
            return;
        }
        if (*digest != work.asset.sha256) {
            cache_.RemoveTemporary(temporaryPath);
            Fail(work, protocol::AssetErrorReason::HashMismatch);
            return;
        }
        SetState(work, AssetState::Verifying, ClientAssetEventKind::Verified);
        if (!cache_.CommitVerified(work.asset, temporaryPath)) {
            cache_.RemoveTemporary(temporaryPath);
            Fail(work, protocol::AssetErrorReason::DiskFailure);
            return;
        }
        SetState(work, AssetState::Ready, ClientAssetEventKind::Ready);
    } catch (...) {
        Fail(work, protocol::AssetErrorReason::InternalError);
    }
}

void ClientAssetManager::SetState(
    const WorkItem& work,
    AssetState state,
    ClientAssetEventKind event,
    protocol::AssetErrorReason error
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (work.generation != generation_) {
        return;
    }
    states_[work.asset.assetId] = state;
    events_.push_back({
        event,
        work.manifestRevision,
        work.asset.assetId,
        0,
        error,
    });
}

void ClientAssetManager::Fail(
    const WorkItem& work,
    protocol::AssetErrorReason reason
) noexcept {
    try {
        SetState(work, AssetState::Failed, ClientAssetEventKind::Failed, reason);
    } catch (...) {
    }
}

bool ClientAssetManager::IsCurrentGeneration(std::uint64_t generation) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return !stopping_ && generation == generation_;
}

}
