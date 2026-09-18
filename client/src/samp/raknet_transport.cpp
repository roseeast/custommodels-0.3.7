#include <custommodel/samp/raknet_transport.hpp>

#include <custommodel/core/logger.hpp>
#include <custommodel/core/module.hpp>
#include <custommodel/samp/compatibility.hpp>
#include <custommodel/transport_frame.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include <windows.h>

namespace custommodel::samp {
namespace {

#define CUSTOMMODEL_FASTCALL __fastcall

#pragma pack(push, 1)
struct RakPlayerId {
    std::uint32_t binaryAddress;
    std::uint16_t port;
};

struct RakPacket {
    std::uint16_t playerIndex;
    RakPlayerId playerId;
    std::uint32_t length;
    std::uint32_t bitSize;
    std::uint8_t* data;
    bool deleteData;
};
#pragma pack(pop)

static_assert(offsetof(RakPacket, length) == 8);
static_assert(offsetof(RakPacket, data) == 16);
static_assert(sizeof(RakPacket) == 21);

enum class RakClientVtableIndex : std::size_t {
    SendRaw = 7,
    Receive = 8,
    DeallocatePacket = 9,
    IsConnected = 18,
};

enum class RakPacketPriority : int {
    High = 1,
};

enum class RakPacketReliability : int {
    ReliableOrdered = 9,
};

using GetRakClientFunction = void* (CUSTOMMODEL_FASTCALL*)(void*, void*);
using SendFunction = bool (CUSTOMMODEL_FASTCALL*)(
    void*, void*, const char*, int, RakPacketPriority, RakPacketReliability, char
);
using ReceiveFunction = RakPacket* (CUSTOMMODEL_FASTCALL*)(void*, void*);
using DeallocateFunction = void (CUSTOMMODEL_FASTCALL*)(void*, void*, RakPacket*);
using IsConnectedFunction = bool (CUSTOMMODEL_FASTCALL*)(void*, void*);

constexpr std::uint8_t kDisconnectionNotification = 32;
constexpr std::uint8_t kConnectionLost = 33;
constexpr std::size_t kRakClientVtableEntriesNeeded =
    static_cast<std::size_t>(RakClientVtableIndex::IsConnected) + 1;
constexpr unsigned kResolveAttempts = 600;
constexpr DWORD kResolveDelayMilliseconds = 50;
constexpr unsigned kMaximumConsumedPacketsPerCall = 64;
constexpr std::size_t kSendPreviewByteLimit = 16;

std::string HexPreview(protocol::ByteView bytes) {
    std::array<char, kSendPreviewByteLimit * 3> preview{};
    std::size_t previewLength{};
    const auto count = std::min(bytes.size, kSendPreviewByteLimit);

    for (std::size_t index = 0; index < count; ++index) {
        const auto written = std::snprintf(
            preview.data() + previewLength,
            preview.size() - previewLength,
            index == 0 ? "%02X" : " %02X",
            static_cast<unsigned>(bytes.data[index])
        );
        if (written <= 0 ||
            static_cast<std::size_t>(written) >= preview.size() - previewLength) {
            break;
        }
        previewLength += static_cast<std::size_t>(written);
    }

    return previewLength == 0 ? "<none>" : std::string{preview.data(), previewLength};
}

bool IsReadable(const void* pointer, std::size_t length) noexcept {
    if (pointer == nullptr || length == 0) {
        return false;
    }

    const auto start = reinterpret_cast<std::uintptr_t>(pointer);
    if (length > std::numeric_limits<std::uintptr_t>::max() - start) {
        return false;
    }

    const auto end = start + length;
    auto current = start;
    while (current < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(current), &memory, sizeof(memory)) == 0 ||
            memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return false;
        }

        const DWORD protection = memory.Protect & 0xFFU;
        if (protection == PAGE_EXECUTE) {
            return false;
        }

        const auto regionStart = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        if (memory.RegionSize > std::numeric_limits<std::uintptr_t>::max() - regionStart) {
            return false;
        }
        const auto next = regionStart + memory.RegionSize;
        if (next <= current) {
            return false;
        }
        current = next;
    }

    return true;
}

template <typename Function>
Function VtableFunction(void** vtable, RakClientVtableIndex index) noexcept {
    return reinterpret_cast<Function>(vtable[static_cast<std::size_t>(index)]);
}

class SampRakNetTransport final : public client_protocol::ClientTransport {
public:
    explicit SampRakNetTransport(SampVersion version) noexcept : version_(version) {}

    bool Initialize(client_protocol::TransportCallbacks callbacks) noexcept override {
        if (initialized_ || callbacks.incoming == nullptr || callbacks.connection == nullptr) {
            return false;
        }

        const auto lookup = core::FindLoadedModule(L"samp.dll");
        if (!lookup || lookup.module->entryPointRva == 0) {
            return false;
        }

        const auto* build = FindSampBuild(lookup.module->entryPointRva);
        const auto* compatibility = FindCompatibility(version_);
        if (build == nullptr || build->version != version_ || compatibility == nullptr ||
            !compatibility->HasAllMappings({
                AddressMapping::NetGame,
                AddressMapping::GetRakClient,
            })) {
            return false;
        }

        module_ = *lookup.module;
        callbacks_ = callbacks;
        for (unsigned attempt = 0; attempt < kResolveAttempts; ++attempt) {
            if (ResolveRakClient(*compatibility)) {
                break;
            }
            Sleep(kResolveDelayMilliseconds);
        }

        initialized_ = rakClient_ != nullptr;
        if (!initialized_ || !InstallReceiveHook()) {
            Shutdown();
            return false;
        }

        core::GetLogger().Info("RakClient resolved");
        core::GetLogger().Info("transport initialized");
        return true;
    }

    void Shutdown() noexcept override {
        if (!RestoreReceiveHook()) {
            OutputDebugStringA(
                "[CustomModel] Receive hook could not be restored; transport remains active\n"
            );
            return;
        }
        auto* expected = this;
        active_.compare_exchange_strong(expected, nullptr);
        initialized_ = false;
        connected_ = false;
        awaitingDisconnectedState_ = false;
        connectedEventPending_ = false;
        connectedEventSent_ = false;
        rakClient_ = nullptr;
        vtable_ = nullptr;
        sendFunction_ = nullptr;
        originalReceive_ = nullptr;
        deallocateFunction_ = nullptr;
        isConnectedFunction_ = nullptr;
        callbacks_ = {};
    }

    bool Send(protocol::ByteView bytes) noexcept override {
        if (!initialized_ || rakClient_ == nullptr || sendFunction_ == nullptr ||
            !protocol::DecodePacket(bytes)) {
            return false;
        }

        try {
            auto frame = transport_frame::Encode(bytes);
            if (!frame.has_value()) {
                return false;
            }
            const auto raw = MakeRakNetRawSendBuffer({frame->data(), frame->size()});
            if (!raw) {
                return false;
            }

            auto& logger = core::GetLogger();
            logger.Debug(std::string{"CMOD bytes="} + std::to_string(bytes.size));
            logger.Debug(std::string{"transport bytes="} + std::to_string(frame->size()));
            logger.Debug(std::string{"RakNet Send length="} + std::to_string(raw.byteLength));
            logger.Debug(std::string{"preview="} + HexPreview({frame->data(), frame->size()}));

            return sendFunction_(
                rakClient_,
                nullptr,
                raw.data,
                raw.byteLength,
                RakPacketPriority::High,
                RakPacketReliability::ReliableOrdered,
                0
            );
        } catch (...) {
            return false;
        }
    }

    bool DispatchIncoming(protocol::ByteView bytes) noexcept override {
        if (!initialized_ || callbacks_.incoming == nullptr) {
            return false;
        }
        callbacks_.incoming(bytes, callbacks_.context);
        return true;
    }

    bool IsAvailable() const noexcept override {
        const auto* compatibility = FindCompatibility(version_);
        return (version_ == SampVersion::R1 || version_ == SampVersion::R3_1) &&
            compatibility != nullptr &&
            compatibility->HasAllMappings({
                AddressMapping::NetGame,
                AddressMapping::GetRakClient,
            });
    }

private:
    static RakPacket* CUSTOMMODEL_FASTCALL ReceiveHook(void* self, void*) noexcept {
        auto* transport = active_.load();
        if (transport == nullptr || transport->originalReceive_ == nullptr) {
            return nullptr;
        }
        return transport->Receive(self);
    }

    bool ResolveRakClient(const SampCompatibility& compatibility) noexcept {
        const auto netGameSlot = module_.ResolveRva(
            compatibility.addresses.netGamePointerRva,
            sizeof(void*)
        );
        const auto getRakClientAddress = module_.ResolveRva(
            compatibility.addresses.getRakClientRva
        );
        if (!netGameSlot.has_value() || !getRakClientAddress.has_value() ||
            !module_.IsAccessible(*netGameSlot, sizeof(void*)) ||
            !module_.IsExecutable(*getRakClientAddress)) {
            return false;
        }

        auto* netGame = *reinterpret_cast<void**>(*netGameSlot);
        if (!IsReadable(netGame, sizeof(void*))) {
            return false;
        }

        const auto getRakClient = reinterpret_cast<GetRakClientFunction>(*getRakClientAddress);
        auto* rakClient = getRakClient(netGame, nullptr);
        if (!IsReadable(rakClient, sizeof(void*))) {
            return false;
        }

        auto** vtable = *reinterpret_cast<void***>(rakClient);
        const auto vtableSize = kRakClientVtableEntriesNeeded * sizeof(void*);
        const auto vtableAddress = reinterpret_cast<std::uintptr_t>(vtable);
        if (!module_.Contains(vtableAddress, vtableSize) ||
            !module_.IsAccessible(vtableAddress, vtableSize)) {
            return false;
        }

        const auto send = VtableFunction<SendFunction>(vtable, RakClientVtableIndex::SendRaw);
        const auto receive = VtableFunction<ReceiveFunction>(vtable, RakClientVtableIndex::Receive);
        const auto deallocate = VtableFunction<DeallocateFunction>(
            vtable,
            RakClientVtableIndex::DeallocatePacket
        );
        const auto isConnected = VtableFunction<IsConnectedFunction>(
            vtable,
            RakClientVtableIndex::IsConnected
        );
        if (!module_.IsExecutable(reinterpret_cast<std::uintptr_t>(send)) ||
            !module_.IsExecutable(reinterpret_cast<std::uintptr_t>(receive)) ||
            !module_.IsExecutable(reinterpret_cast<std::uintptr_t>(deallocate)) ||
            !module_.IsExecutable(reinterpret_cast<std::uintptr_t>(isConnected))) {
            return false;
        }

        rakClient_ = rakClient;
        vtable_ = vtable;
        sendFunction_ = send;
        originalReceive_ = receive;
        deallocateFunction_ = deallocate;
        isConnectedFunction_ = isConnected;
        return true;
    }

    bool InstallReceiveHook() noexcept {
        if (vtable_ == nullptr || originalReceive_ == nullptr) {
            return false;
        }

        auto** slot = &vtable_[static_cast<std::size_t>(RakClientVtableIndex::Receive)];
        DWORD oldProtection{};
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtection)) {
            return false;
        }

        active_.store(this);
        const auto previous = InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(slot),
            reinterpret_cast<PVOID>(&ReceiveHook),
            reinterpret_cast<PVOID>(originalReceive_)
        );
        DWORD ignored{};
        const bool restoredProtection = VirtualProtect(
            slot,
            sizeof(void*),
            oldProtection,
            &ignored
        ) != FALSE;
        if (previous != reinterpret_cast<PVOID>(originalReceive_)) {
            active_.store(nullptr);
            return false;
        }

        if (!restoredProtection) {
            InterlockedCompareExchangePointer(
                reinterpret_cast<PVOID volatile*>(slot),
                reinterpret_cast<PVOID>(originalReceive_),
                reinterpret_cast<PVOID>(&ReceiveHook)
            );
            VirtualProtect(slot, sizeof(void*), oldProtection, &ignored);
            active_.store(nullptr);
            return false;
        }

        receiveSlot_ = slot;
        return true;
    }

    bool RestoreReceiveHook() noexcept {
        if (receiveSlot_ == nullptr || originalReceive_ == nullptr) {
            return true;
        }

        DWORD oldProtection{};
        if (!VirtualProtect(receiveSlot_, sizeof(void*), PAGE_READWRITE, &oldProtection)) {
            return false;
        }

        const auto previous = InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(receiveSlot_),
            reinterpret_cast<PVOID>(originalReceive_),
            reinterpret_cast<PVOID>(&ReceiveHook)
        );
        DWORD ignored{};
        VirtualProtect(receiveSlot_, sizeof(void*), oldProtection, &ignored);
        if (previous != reinterpret_cast<PVOID>(&ReceiveHook) &&
            previous != reinterpret_cast<PVOID>(originalReceive_)) {
            return false;
        }

        receiveSlot_ = nullptr;
        return true;
    }

    void ObserveConnection(void* self) noexcept {
        const bool connected = isConnectedFunction_(self, nullptr);
        if (awaitingDisconnectedState_) {
            if (connected) {
                return;
            }
            awaitingDisconnectedState_ = false;
            return;
        }
        if (connected == connected_) {
            return;
        }

        connected_ = connected;
        connectedEventPending_ = false;
        const bool notifyDisconnected = connectedEventSent_ && !connected;
        connectedEventSent_ = false;
        if (notifyDisconnected) {
            callbacks_.connection(
                client_protocol::ConnectionEvent::Disconnected,
                callbacks_.context
            );
        }
    }

    void NotifyConnectedIfPending() noexcept {
        if (!connected_ || !connectedEventPending_ || connectedEventSent_) {
            return;
        }

        connectedEventPending_ = false;
        connectedEventSent_ = true;
        callbacks_.connection(
            client_protocol::ConnectionEvent::Connected,
            callbacks_.context
        );
    }

    void NotifyDisconnected() noexcept {
        if (!connected_) {
            return;
        }
        connected_ = false;
        awaitingDisconnectedState_ = true;
        connectedEventPending_ = false;
        const bool notify = connectedEventSent_;
        connectedEventSent_ = false;
        if (notify) {
            callbacks_.connection(
                client_protocol::ConnectionEvent::Disconnected,
                callbacks_.context
            );
        }
    }

    RakPacket* Receive(void* self) noexcept {
        if (self != rakClient_) {
            return originalReceive_(self, nullptr);
        }

        ObserveConnection(self);
        if (connectedEventSent_ && callbacks_.pump != nullptr) {
            callbacks_.pump(callbacks_.context);
        }
        for (unsigned consumed = 0; consumed < kMaximumConsumedPacketsPerCall; ++consumed) {
            auto* packet = originalReceive_(self, nullptr);
            NotifyConnectedIfPending();
            if (packet == nullptr || !IsReadable(packet, sizeof(RakPacket))) {
                return packet;
            }

            // Let SA-MP process its first packet and send ClientJoin before emitting Hello.
            if (connected_ && !connectedEventSent_) {
                connectedEventPending_ = true;
            }

            if (packet->data == nullptr || packet->length == 0 ||
                !IsReadable(packet->data, std::min<std::size_t>(packet->length, 5))) {
                return packet;
            }

            if (packet->data[0] == kDisconnectionNotification ||
                packet->data[0] == kConnectionLost) {
                NotifyDisconnected();
                return packet;
            }

            const protocol::ByteView frame{packet->data, packet->length};
            const auto inspected = transport_frame::Inspect(frame);
            if (inspected.kind == transport_frame::FrameKind::Unrelated) {
                return packet;
            }

            if (packet->length <= transport_frame::kMaximumFrameSize &&
                IsReadable(packet->data, packet->length)) {
                DispatchIncoming(inspected.protocolBytes);
            }
            deallocateFunction_(self, nullptr, packet);
        }

        return nullptr;
    }

    static std::atomic<SampRakNetTransport*> active_;

    SampVersion version_{SampVersion::Unknown};
    core::ModuleInfo module_{};
    client_protocol::TransportCallbacks callbacks_{};
    void* rakClient_{};
    void** vtable_{};
    void** receiveSlot_{};
    SendFunction sendFunction_{};
    ReceiveFunction originalReceive_{};
    DeallocateFunction deallocateFunction_{};
    IsConnectedFunction isConnectedFunction_{};
    bool initialized_{};
    bool connected_{};
    bool awaitingDisconnectedState_{};
    bool connectedEventPending_{};
    bool connectedEventSent_{};
};

std::atomic<SampRakNetTransport*> SampRakNetTransport::active_{nullptr};

SampRakNetTransport g_r1Transport{SampVersion::R1};
SampRakNetTransport g_r31Transport{SampVersion::R3_1};

#undef CUSTOMMODEL_FASTCALL

}

client_protocol::ClientTransport* FindRakNetTransport(SampVersion version) noexcept {
    switch (version) {
    case SampVersion::R1:
        return &g_r1Transport;
    case SampVersion::R3_1:
        return &g_r31Transport;
    default:
        return nullptr;
    }
}

}
