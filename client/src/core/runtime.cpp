#include <custommodel/core/runtime.hpp>

#include <custommodel/assets/fetch.hpp>
#include <custommodel/assets/manager.hpp>
#include <custommodel/core/logger.hpp>
#include <custommodel/core/module.hpp>
#include <custommodel/protocol/handshake.hpp>
#include <custommodel/protocol/transport.hpp>
#include <custommodel/samp/compatibility.hpp>
#include <custommodel/version.hpp>

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

namespace custommodel::core {
namespace {

ClientRuntime g_runtime;

struct ProtocolContext {
    std::optional<client_protocol::ClientHandshake> handshake;
    client_protocol::ClientTransport* transport{};
    client_assets::ClientAssetManager* assets{};
    bool malformedPacketLogged{};
};

ProtocolContext g_protocolContext;

std::string Hex(std::uintptr_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << value;
    return stream.str();
}

DWORD WINAPI InitializationThread(LPVOID parameter) {
    GetRuntime().Initialize(static_cast<HMODULE>(parameter));
    return 0;
}

std::filesystem::path ModuleDirectory(HMODULE module) {
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(
        module,
        path.data(),
        static_cast<DWORD>(path.size())
    );
    if (length == 0 || length == path.size()) {
        return {};
    }
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
}

void LogFetchDiagnostic(std::string_view message, void*) noexcept {
    GetLogger().Debug(message);
}

void SendAssetReport(
    ProtocolContext& context,
    const protocol::Message& message
) noexcept {
    if (context.transport == nullptr) {
        return;
    }
    try {
        const auto encoded = protocol::EncodePacket(message);
        if (!encoded || !context.transport->Send({encoded.bytes.data(), encoded.bytes.size()})) {
            GetLogger().Error("asset status send failed");
        }
    } catch (...) {
        GetLogger().Error("asset status encoding failed");
    }
}

void PumpAssetResults(void* context) noexcept {
    auto& protocolContext = *static_cast<ProtocolContext*>(context);
    if (protocolContext.assets == nullptr || !protocolContext.handshake.has_value() ||
        protocolContext.handshake->State() != client_protocol::HandshakeState::Compatible) {
        return;
    }

    try {
        for (const auto& event : protocolContext.assets->DrainEvents()) {
            switch (event.kind) {
            case client_assets::ClientAssetEventKind::ManifestBegin:
                GetLogger().Info(
                    std::string{"manifest revision "} +
                    std::to_string(event.manifestRevision) + " received"
                );
                GetLogger().Info(
                    std::string{"manifest contains "} +
                    std::to_string(event.assetCount) + " assets"
                );
                break;
            case client_assets::ClientAssetEventKind::ManifestComplete:
                break;
            case client_assets::ClientAssetEventKind::CacheHit:
                GetLogger().Info(
                    std::string{"asset "} + std::to_string(event.assetId) + " cache hit"
                );
                break;
            case client_assets::ClientAssetEventKind::Queued:
                GetLogger().Info(
                    std::string{"asset "} + std::to_string(event.assetId) + " queued"
                );
                break;
            case client_assets::ClientAssetEventKind::Downloading:
                GetLogger().Info(
                    std::string{"asset "} + std::to_string(event.assetId) + " downloading"
                );
                break;
            case client_assets::ClientAssetEventKind::Verifying:
                GetLogger().Info(
                    std::string{"asset "} + std::to_string(event.assetId) + " verifying"
                );
                break;
            case client_assets::ClientAssetEventKind::Verified:
                GetLogger().Info(
                    std::string{"asset "} + std::to_string(event.assetId) + " verified"
                );
                break;
            case client_assets::ClientAssetEventKind::Ready:
                GetLogger().Info(
                    std::string{"asset "} + std::to_string(event.assetId) + " ready"
                );
                SendAssetReport(protocolContext, protocol::AssetReady{
                    event.manifestRevision,
                    event.assetId,
                });
                break;
            case client_assets::ClientAssetEventKind::Failed:
                GetLogger().Info(
                    std::string{"asset "} + std::to_string(event.assetId) +
                    " failed: " + protocol::ToString(event.error)
                );
                SendAssetReport(protocolContext, protocol::AssetError{
                    event.manifestRevision,
                    event.assetId,
                    event.error,
                });
                break;
            }
        }
    } catch (...) {
        GetLogger().Error("asset result processing failed");
    }
}

void HandleIncomingProtocol(protocol::ByteView bytes, void* context) noexcept {
    auto& protocolContext = *static_cast<ProtocolContext*>(context);
    if (!protocolContext.handshake.has_value()) {
        return;
    }

    const auto decoded = protocol::DecodePacket(bytes);
    if (!decoded || !decoded.packet.has_value()) {
        if (!protocolContext.malformedPacketLogged) {
            GetLogger().Error("malformed CustomModel packet dropped");
            protocolContext.malformedPacketLogged = true;
        }
        return;
    }

    const bool isHandshakeMessage =
        std::holds_alternative<protocol::Welcome>(decoded.packet->message) ||
        std::holds_alternative<protocol::Reject>(decoded.packet->message);
    if (!isHandshakeMessage) {
        if (protocolContext.handshake->State() !=
                client_protocol::HandshakeState::Compatible ||
            protocolContext.assets == nullptr) {
            return;
        }
        try {
            const auto result = protocolContext.assets->HandleMessage(decoded.packet->message);
            if (result == client_assets::ManifestInputResult::InvalidManifest ||
                result == client_assets::ManifestInputResult::InvalidOrder) {
                GetLogger().Error("invalid asset manifest sequence dropped");
                if (const auto* item = std::get_if<protocol::ManifestAsset>(
                        &decoded.packet->message
                    )) {
                    SendAssetReport(protocolContext, protocol::AssetError{
                        item->manifestRevision,
                        item->asset.assetId,
                        protocol::AssetErrorReason::InvalidManifest,
                    });
                }
            }
        } catch (...) {
            GetLogger().Error("asset manifest processing failed");
        }
        PumpAssetResults(context);
        return;
    }

    const auto result = protocolContext.handshake->HandleIncoming(bytes);
    switch (result) {
    case client_protocol::IncomingResult::WelcomeReceived:
        GetLogger().Info("Welcome received");
        if (protocolContext.handshake->ConfirmWelcome()) {
            GetLogger().Info("handshake complete");
        }
        break;
    case client_protocol::IncomingResult::Rejected:
        if (protocolContext.handshake->Rejection().has_value()) {
            switch (protocolContext.handshake->Rejection()->reason) {
            case protocol::RejectReason::UnsupportedProtocol:
                GetLogger().Info("CustomModel handshake rejected: unsupported protocol");
                break;
            case protocol::RejectReason::UnsupportedClientRuntime:
                GetLogger().Info("CustomModel handshake rejected: unsupported client runtime");
                break;
            case protocol::RejectReason::UnsupportedSampBuild:
                GetLogger().Info("CustomModel handshake rejected: unsupported SA-MP build");
                break;
            case protocol::RejectReason::MalformedHandshake:
                GetLogger().Info("CustomModel handshake rejected: malformed handshake");
                break;
            case protocol::RejectReason::UnsupportedCapabilities:
                GetLogger().Info("CustomModel handshake rejected: unsupported capabilities");
                break;
            }
        } else {
            GetLogger().Info("CustomModel handshake rejected");
        }
        break;
    case client_protocol::IncomingResult::Malformed:
        if (!protocolContext.malformedPacketLogged) {
            GetLogger().Error("malformed CustomModel packet dropped");
            protocolContext.malformedPacketLogged = true;
        }
        break;
    case client_protocol::IncomingResult::Ignored:
        break;
    }
}

void HandleConnectionEvent(
    client_protocol::ConnectionEvent event,
    void* context
) noexcept {
    auto& protocolContext = *static_cast<ProtocolContext*>(context);
    if (!protocolContext.handshake.has_value() || protocolContext.transport == nullptr) {
        return;
    }

    if (event == client_protocol::ConnectionEvent::Disconnected) {
        protocolContext.handshake->OnDisconnected();
        if (protocolContext.assets != nullptr) {
            protocolContext.assets->ResetSession();
        }
        protocolContext.malformedPacketLogged = false;
        GetLogger().Info("RakNet connection closed");
        return;
    }

    try {
        if (protocolContext.assets != nullptr) {
            protocolContext.assets->ResetSession();
        }
        protocolContext.handshake->OnConnected();
        GetLogger().Info("RakNet connection established");
        auto hello = protocolContext.handshake->TakeHello();
        if (!hello.has_value()) {
            GetLogger().Error("could not encode Hello");
            return;
        }
        if (!protocolContext.transport->Send({hello->data(), hello->size()})) {
            GetLogger().Error("Hello send failed");
            return;
        }
        GetLogger().Info("Hello sent");
    } catch (...) {
        GetLogger().Error("Hello creation failed");
    }
}

}

void ClientRuntime::Initialize(HMODULE module) noexcept {
    RuntimeState expected = RuntimeState::Dormant;
    if (!state_.compare_exchange_strong(expected, RuntimeState::Initializing)) {
        return;
    }

    try {
        InitializeImpl(module);
    } catch (const std::exception& exception) {
        Disable(std::string{"initialization failed: "} + exception.what());
    } catch (...) {
        Disable("initialization failed with an unknown error");
    }
}

void ClientRuntime::InitializeImpl(HMODULE module) {
    auto& logger = GetLogger();
    logger.Open(module);
    logger.Info("initializing");

    if (state_.load() != RuntimeState::Initializing) {
        return;
    }

    const auto lookup = FindLoadedModule(L"samp.dll");
    if (!lookup) {
        logger.Error(std::string{"samp.dll detection failed: "} + lookup.error);
        Disable("unsupported SA-MP build");
        return;
    }

    const auto& sampModule = *lookup.module;
    logger.Info("samp.dll found");
    logger.Info(std::string{"samp.dll base: "} + Hex(sampModule.base));
    logger.Info(std::string{"samp.dll PE entry point RVA: "} + Hex(sampModule.entryPointRva));

    if (!sampModule.isPe32) {
        Disable("unsupported non-PE32 samp.dll");
        return;
    }

    const auto* build = FindSampBuild(sampModule.entryPointRva);
    if (build == nullptr) {
        Disable("unsupported SA-MP build");
        return;
    }

    logger.Info(std::string{build->name} + " detected");

    const auto* compatibility = samp::FindCompatibility(build->version);
    if (compatibility == nullptr) {
        Disable("no compatibility registry entry for the detected SA-MP build");
        return;
    }

    logger.Info("compatibility layer initialized");
    logger.Info("CustomModel protocol initialized");

    auto& transport = client_protocol::SelectSampTransport(build->version);
    g_protocolContext.handshake.emplace(client_protocol::HandshakeConfiguration{
        kRuntimeVersion,
        build->version,
        protocol::kDeclaredCapabilities,
    });
    g_protocolContext.transport = &transport;
    if (transport.IsAvailable()) {
        const auto directory = ModuleDirectory(module);
        auto fetch = client_assets::CreateWinHttpFetchBackend(
            &LogFetchDiagnostic,
            nullptr
        );
        if (!directory.empty() && fetch != nullptr) {
            auto assetManager = std::make_unique<client_assets::ClientAssetManager>(
                directory / L"CustomModel" / L"cache",
                std::move(fetch)
            );
            if (assetManager->Initialize()) {
                g_protocolContext.assets = assetManager.release();
            } else {
                logger.Error("asset subsystem initialization failed");
            }
        }
    }
    if (!transport.IsAvailable() || g_protocolContext.assets == nullptr ||
        !transport.Initialize({
        HandleIncomingProtocol,
        HandleConnectionEvent,
        PumpAssetResults,
        &g_protocolContext,
    })) {
        logger.Info("SA-MP transport unavailable for this build");
        transport.Shutdown();
        g_protocolContext.transport = nullptr;
        if (g_protocolContext.assets != nullptr) {
            g_protocolContext.assets->Shutdown();
            delete g_protocolContext.assets;
            g_protocolContext.assets = nullptr;
        }
    }

    RuntimeState expected = RuntimeState::Initializing;
    state_.compare_exchange_strong(expected, RuntimeState::Ready);
}

void ClientRuntime::Disable(std::string_view reason) noexcept {
    GetLogger().Info(reason);
    GetLogger().Info("custom model functionality disabled");
    RuntimeState expected = RuntimeState::Initializing;
    state_.compare_exchange_strong(expected, RuntimeState::Disabled);
}

void ClientRuntime::RequestShutdownFromLoaderLock() noexcept {
    state_.store(RuntimeState::ShuttingDown);
    if (g_protocolContext.transport != nullptr) {
        g_protocolContext.transport->Shutdown();
        g_protocolContext.transport = nullptr;
    }
    if (g_protocolContext.handshake.has_value()) {
        g_protocolContext.handshake->OnDisconnected();
    }
    if (g_protocolContext.assets != nullptr) {
        g_protocolContext.assets->RequestStop();
    }
}

RuntimeState ClientRuntime::State() const noexcept {
    return state_.load();
}

ClientRuntime& GetRuntime() noexcept {
    return g_runtime;
}

bool ScheduleInitialization(HMODULE module) noexcept {
    const auto thread = CreateThread(
        nullptr,
        0,
        InitializationThread,
        module,
        0,
        nullptr
    );
    if (thread == nullptr) {
        return false;
    }

    CloseHandle(thread);
    return true;
}

}
