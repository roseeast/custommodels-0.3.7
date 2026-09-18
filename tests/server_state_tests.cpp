#include <custommodel/server/handshake_endpoint.hpp>
#include <custommodel/server/player_capabilities.hpp>
#include <custommodel/server/transport.hpp>
#include <custommodel/protocol/handshake.hpp>
#include <custommodel/transport_frame.hpp>

#include <cstdint>
#include <iostream>
#include <string_view>
#include <variant>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

custommodel::protocol::CapabilityFlags Flag(custommodel::protocol::Capability capability) {
    return static_cast<custommodel::protocol::CapabilityFlags>(capability);
}

custommodel::protocol::Hello ValidHello() {
    using namespace custommodel;
    return {
        kRuntimeVersion,
        protocol::kProtocolVersion,
        SampVersion::R1,
        Flag(protocol::Capability::CharModel) |
            Flag(protocol::Capability::ObjectModel) |
            Flag(protocol::Capability::Dff),
    };
}

custommodel::protocol::RejectReason RejectionReason(
    const custommodel::server::HandshakeResponse& response
) {
    const auto* reject = std::get_if<custommodel::protocol::Reject>(&response);
    return reject == nullptr
        ? custommodel::protocol::RejectReason::MalformedHandshake
        : reject->reason;
}

class FakeServerTransport final : public custommodel::server::ServerTransport {
public:
    bool Initialize(custommodel::server::IncomingHandler, void*) noexcept override {
        available = true;
        return true;
    }

    void Shutdown() noexcept override {
        available = false;
    }

    bool Send(
        std::uint32_t playerId,
        custommodel::protocol::ByteView bytes
    ) noexcept override {
        if (!sendSucceeds || bytes.data == nullptr) {
            return false;
        }
        try {
            sentPlayer = playerId;
            sent.assign(bytes.data, bytes.data + bytes.size);
            return true;
        } catch (...) {
            return false;
        }
    }

    bool DispatchIncoming(
        std::uint32_t,
        custommodel::protocol::ByteView
    ) noexcept override {
        return false;
    }

    bool IsAvailable() const noexcept override {
        return available;
    }

    bool available{true};
    bool sendSucceeds{true};
    std::uint32_t sentPlayer{};
    std::vector<std::uint8_t> sent;
};

std::vector<std::uint8_t> Frame(const custommodel::protocol::Message& message) {
    const auto packet = custommodel::protocol::EncodePacket(message);
    const auto frame = custommodel::transport_frame::Encode({
        packet.bytes.data(),
        packet.bytes.size(),
    });
    return frame.value_or(std::vector<std::uint8_t>{});
}

std::vector<std::uint8_t> ProtocolPacket(const custommodel::protocol::Message& message) {
    return custommodel::protocol::EncodePacket(message).bytes;
}

void TestCompatibleTransition() {
    using namespace custommodel;
    server::HandshakePolicy policy{};
    policy.serverCapabilities = Flag(protocol::Capability::CharModel) |
        Flag(protocol::Capability::Dff) |
        Flag(protocol::Capability::Txd);
    server::PlayerCapabilityTracker tracker{policy};

    Check(
        tracker.GetState(12).state == server::HandshakeState::Unknown,
        "untracked player should be Unknown"
    );
    tracker.BeginHandshake(12);
    Check(
        tracker.GetState(12).state == server::HandshakeState::HandshakePending,
        "connected player should become HandshakePending"
    );

    const auto response = tracker.HandleHello(12, ValidHello());
    const auto* welcome = std::get_if<protocol::Welcome>(&response);
    Check(welcome != nullptr, "valid Hello should produce Welcome");
    const auto state = tracker.GetState(12);
    Check(state.state == server::HandshakeState::Compatible, "valid Hello should mark player Compatible");
    Check(state.protocolVersion == protocol::kProtocolVersion, "client protocol should be stored");
    Check(state.runtimeVersion.minor == kRuntimeVersion.minor, "client runtime should be stored");
    Check(state.sampVersion == SampVersion::R1, "client SA-MP build should be stored");
    Check(state.declaredCapabilities == ValidHello().capabilities, "declared capabilities should be stored");
    const auto expected = Flag(protocol::Capability::CharModel) | Flag(protocol::Capability::Dff);
    Check(state.negotiatedCapabilities == expected, "capabilities should be intersected with server support");
    if (welcome != nullptr) {
        Check(welcome->acceptedProtocolVersion == protocol::kProtocolVersion, "Welcome should accept version 1");
        Check(welcome->serverProtocolVersion == protocol::kProtocolVersion, "Welcome should declare server protocol");
        Check(welcome->negotiatedCapabilities == expected, "Welcome should carry negotiated capabilities");
    }

    tracker.RemovePlayer(12);
    Check(tracker.GetState(12).state == server::HandshakeState::Unknown, "disconnect should return to Unknown");
    Check(tracker.TrackedPlayerCount() == 0, "disconnect should erase per-player state");
}

void TestTypedRejections() {
    using namespace custommodel;
    server::PlayerCapabilityTracker tracker;

    tracker.BeginHandshake(1);
    auto hello = ValidHello();
    hello.protocolVersion = protocol::kProtocolVersion + 1;
    auto response = tracker.HandleHello(1, hello);
    Check(
        RejectionReason(response) == protocol::RejectReason::UnsupportedProtocol,
        "unsupported Hello protocol should receive a typed rejection"
    );
    Check(tracker.GetState(1).state == server::HandshakeState::Rejected, "protocol rejection should update state");

    tracker.BeginHandshake(2);
    hello = ValidHello();
    hello.runtimeVersion.major = static_cast<std::uint16_t>(kRuntimeVersion.major + 1);
    response = tracker.HandleHello(2, hello);
    Check(
        RejectionReason(response) == protocol::RejectReason::UnsupportedClientRuntime,
        "unsupported runtime should receive a typed rejection"
    );

    tracker.BeginHandshake(3);
    hello = ValidHello();
    hello.sampVersion = SampVersion::R3;
    response = tracker.HandleHello(3, hello);
    Check(
        RejectionReason(response) == protocol::RejectReason::UnsupportedSampBuild,
        "an enum value without a build registry record should be unsupported"
    );

    tracker.BeginHandshake(4);
    hello = ValidHello();
    hello.capabilities = 1U << 31U;
    response = tracker.HandleHello(4, hello);
    Check(
        RejectionReason(response) == protocol::RejectReason::MalformedHandshake,
        "invalid capability bits should be a malformed handshake"
    );

    response = tracker.HandleHello(5, ValidHello());
    Check(
        RejectionReason(response) == protocol::RejectReason::MalformedHandshake,
        "Hello before HandshakePending should be rejected"
    );

    tracker.BeginHandshake(6);
    static_cast<void>(tracker.HandleHello(6, ValidHello()));
    const auto reject = tracker.RejectPlayer(6, protocol::RejectReason::MalformedHandshake);
    Check(reject.reason == protocol::RejectReason::MalformedHandshake, "explicit parse rejection should be typed");
    Check(tracker.GetState(6).state == server::HandshakeState::Rejected, "explicit rejection should update state");
    Check(
        tracker.GetState(6).declaredCapabilities == 0,
        "unparseable rejection should not retain stale declarations"
    );
}

void TestCapabilityPolicy() {
    using namespace custommodel;
    server::HandshakePolicy policy{};
    policy.serverCapabilities = Flag(protocol::Capability::CharModel) |
        Flag(protocol::Capability::Dff) |
        Flag(protocol::Capability::Txd);
    policy.requiredCapabilities = Flag(protocol::Capability::Dff) |
        Flag(protocol::Capability::Txd);
    server::PlayerCapabilityTracker tracker{policy};

    tracker.BeginHandshake(10);
    auto hello = ValidHello();
    Check(
        RejectionReason(tracker.HandleHello(10, hello)) ==
            protocol::RejectReason::UnsupportedCapabilities,
        "missing a mandatory negotiated capability should receive a typed rejection"
    );

    tracker.BeginHandshake(10);
    hello.capabilities |= Flag(protocol::Capability::Txd);
    const auto response = tracker.HandleHello(10, hello);
    const auto* welcome = std::get_if<protocol::Welcome>(&response);
    Check(welcome != nullptr, "all mandatory capabilities should allow Welcome");
    if (welcome != nullptr) {
        const auto expected = Flag(protocol::Capability::CharModel) |
            Flag(protocol::Capability::Dff) |
            Flag(protocol::Capability::Txd);
        Check(
            welcome->negotiatedCapabilities == expected,
            "Welcome should contain only the capability intersection"
        );
    }
}

void TestTransportBuildPolicy() {
    using namespace custommodel;
    server::PlayerCapabilityTracker tracker;

    tracker.BeginHandshake(20);
    auto hello = ValidHello();
    hello.sampVersion = SampVersion::R3_1;
    Check(
        std::holds_alternative<protocol::Welcome>(tracker.HandleHello(20, hello)),
        "R3-1 should be accepted by the Phase 2B server policy"
    );

    tracker.BeginHandshake(21);
    hello.sampVersion = SampVersion::R2;
    Check(
        RejectionReason(tracker.HandleHello(21, hello)) ==
            protocol::RejectReason::UnsupportedSampBuild,
        "R2 should remain disabled by the Phase 2B server policy"
    );

    tracker.BeginHandshake(22);
    hello.sampVersion = SampVersion::R4;
    Check(
        RejectionReason(tracker.HandleHello(22, hello)) ==
            protocol::RejectReason::UnsupportedSampBuild,
        "the public-reference R4 record should remain disabled"
    );

    tracker.BeginHandshake(23);
    hello.sampVersion = SampVersion::R4Family;
    Check(
        RejectionReason(tracker.HandleHello(23, hello)) ==
            protocol::RejectReason::UnsupportedSampBuild,
        "the runtime-observed R4-family record should remain disabled"
    );
}

void TestServerTransportBoundary() {
    auto& transport = custommodel::server::UnavailableServerTransport();
    Check(!transport.IsAvailable(), "server SDK transport should remain unavailable");
    Check(!transport.Initialize(nullptr, nullptr), "unavailable server transport should not initialize");
    Check(!transport.Send(7, {nullptr, 0}), "unavailable server transport should not send");
    Check(
        !transport.DispatchIncoming(7, {nullptr, 0}),
        "unavailable server transport should not dispatch"
    );
    transport.Shutdown();
}

void TestHandshakeEndpoint() {
    using namespace custommodel;
    server::HandshakePolicy policy{};
    policy.serverCapabilities = Flag(protocol::Capability::CharModel) |
        Flag(protocol::Capability::Dff);
    server::PlayerCapabilityTracker tracker{policy};
    FakeServerTransport transport;
    server::HandshakeEndpoint endpoint{tracker, transport};

    endpoint.OnPlayerConnected(40);
    const auto helloFrame = Frame(ValidHello());
    Check(
        endpoint.DispatchIncoming(40, {helloFrame.data(), helloFrame.size()}) ==
            server::DispatchResult::WelcomeSent,
        "server endpoint should answer a valid Hello with Welcome"
    );
    Check(transport.sentPlayer == 40, "server response should target the Hello sender");
    const auto welcomeFrame = transport_frame::Inspect(transport.sent);
    const auto welcomePacket = protocol::DecodePacket(welcomeFrame.protocolBytes);
    Check(
        welcomeFrame.kind == transport_frame::FrameKind::CustomModel &&
            welcomePacket.packet.has_value() &&
            std::holds_alternative<protocol::Welcome>(welcomePacket.packet->message),
        "server Welcome should use the RakNet carrier plus CMOD framing"
    );

    const std::vector<std::uint8_t> ordinary{200, 1, 2, 3};
    transport.sent.clear();
    Check(
        endpoint.DispatchIncoming(40, {ordinary.data(), ordinary.size()}) ==
            server::DispatchResult::Unrelated,
        "unrelated packets should bypass the server CMOD decoder"
    );
    Check(transport.sent.empty(), "unrelated packets should not produce a response");

    endpoint.OnPlayerConnected(41);
    const std::vector<std::uint8_t> malformed{
        transport_frame::kRakNetPacketId, 'C', 'M', 'O', 'D',
    };
    Check(
        endpoint.DispatchIncoming(41, {malformed.data(), malformed.size()}) ==
            server::DispatchResult::RejectSent,
        "malformed CMOD handshake should receive Reject"
    );
    Check(
        tracker.GetState(41).state == server::HandshakeState::Rejected,
        "malformed CMOD handshake should reject server capability state"
    );
    const auto rejectFrame = transport_frame::Inspect(transport.sent);
    const auto rejectPacket = protocol::DecodePacket(rejectFrame.protocolBytes);
    const auto* reject = rejectPacket.packet.has_value()
        ? std::get_if<protocol::Reject>(&rejectPacket.packet->message)
        : nullptr;
    Check(
        reject != nullptr && reject->reason == protocol::RejectReason::MalformedHandshake,
        "malformed handshake response should carry the typed reason"
    );

    endpoint.OnPlayerDisconnected(41);
    Check(
        tracker.GetState(41).state == server::HandshakeState::Unknown,
        "server disconnect should remove endpoint capability state"
    );
}

void TestDuplicateHelloAndReconnect() {
    using namespace custommodel;
    server::HandshakePolicy policy{};
    policy.serverCapabilities = protocol::kDeclaredCapabilities;
    server::PlayerCapabilityTracker tracker{policy};
    FakeServerTransport transport;
    server::HandshakeEndpoint endpoint{tracker, transport};
    const auto helloFrame = Frame(ValidHello());

    endpoint.OnPlayerConnected(50);
    Check(
        endpoint.DispatchIncoming(50, {helloFrame.data(), helloFrame.size()}) ==
            server::DispatchResult::WelcomeSent,
        "first Hello should receive Welcome"
    );
    Check(
        endpoint.DispatchIncoming(50, {helloFrame.data(), helloFrame.size()}) ==
            server::DispatchResult::WelcomeSent,
        "an identical duplicate Hello should idempotently resend Welcome"
    );
    Check(
        tracker.GetState(50).state == server::HandshakeState::Compatible,
        "an identical duplicate Hello should preserve Compatible state"
    );

    endpoint.OnPlayerDisconnected(50);
    endpoint.OnPlayerConnected(50);
    Check(
        tracker.GetState(50).state == server::HandshakeState::HandshakePending,
        "reused player IDs should start a new pending session"
    );
    Check(
        endpoint.DispatchIncoming(50, {helloFrame.data(), helloFrame.size()}) ==
            server::DispatchResult::WelcomeSent,
        "a reconnect should accept a fresh Hello"
    );

    tracker.Reset();
    Check(tracker.TrackedPlayerCount() == 0, "server reset should clear every player session");
}

void TestServerDispatchedPayloadBoundary() {
    using namespace custommodel;
    server::HandshakePolicy policy{};
    policy.serverCapabilities = protocol::kDeclaredCapabilities;
    server::PlayerCapabilityTracker tracker{policy};
    FakeServerTransport transport;
    server::HandshakeEndpoint endpoint{tracker, transport};

    endpoint.OnPlayerConnected(80);
    const auto hello = ProtocolPacket(ValidHello());
    Check(
        endpoint.DispatchProtocolPayload(80, {hello.data(), hello.size()}) ==
            server::DispatchResult::WelcomeSent,
        "the open.mp handler payload should begin at CMOD and produce Welcome"
    );

    endpoint.OnPlayerConnected(81);
    const std::vector<std::uint8_t> wrongMagic{'N', 'O', 'P', 'E', 1, 2, 3};
    transport.sent.clear();
    Check(
        endpoint.DispatchProtocolPayload(81, {wrongMagic.data(), wrongMagic.size()}) ==
            server::DispatchResult::Unrelated,
        "wrong magic after open.mp dispatch should remain unrelated"
    );
    Check(transport.sent.empty(), "unrelated dispatched payload should not produce a response");

    endpoint.OnPlayerConnected(82);
    const auto helloFrame = Frame(ValidHello());
    Check(
        endpoint.DispatchProtocolPayload(82, {helloFrame.data(), helloFrame.size()}) ==
            server::DispatchResult::Unrelated,
        "the open.mp payload boundary should not expect the 0x5E carrier again"
    );

    endpoint.OnPlayerConnected(82);
    const std::vector<std::uint8_t> truncated{'C', 'M', 'O', 'D'};
    Check(
        endpoint.DispatchProtocolPayload(82, {truncated.data(), truncated.size()}) ==
            server::DispatchResult::RejectSent,
        "truncated claimed CMOD payload should receive Reject"
    );
    Check(
        tracker.GetState(82).rejectReason == protocol::RejectReason::MalformedHandshake,
        "truncated claimed CMOD payload should record a malformed rejection"
    );

    endpoint.OnPlayerConnected(83);
    auto oversized = hello;
    oversized.resize(protocol::kMaximumPacketSize + 1, 0);
    Check(
        endpoint.DispatchProtocolPayload(83, {oversized.data(), oversized.size()}) ==
            server::DispatchResult::RejectSent,
        "oversized dispatched CMOD payload should receive Reject"
    );

    endpoint.OnPlayerConnected(84);
    Check(
        endpoint.DispatchProtocolPayload(84, {hello.data(), hello.size()}) ==
            server::DispatchResult::WelcomeSent &&
            endpoint.DispatchProtocolPayload(84, {hello.data(), hello.size()}) ==
                server::DispatchResult::WelcomeSent,
        "a duplicate dispatched Hello should idempotently resend Welcome"
    );
}

void TestEndpointProtocolRejection() {
    using namespace custommodel;
    server::PlayerCapabilityTracker tracker;
    FakeServerTransport transport;
    server::HandshakeEndpoint endpoint{tracker, transport};
    endpoint.OnPlayerConnected(60);

    auto invalidProtocol = Frame(ValidHello());
    invalidProtocol[5] = 0;
    invalidProtocol[6] = static_cast<std::uint8_t>(protocol::kProtocolVersion + 1);
    Check(
        endpoint.DispatchIncoming(60, {invalidProtocol.data(), invalidProtocol.size()}) ==
            server::DispatchResult::RejectSent,
        "an unsupported outer protocol should produce Reject"
    );
    Check(
        tracker.GetState(60).rejectReason == protocol::RejectReason::UnsupportedProtocol,
        "outer protocol rejection should retain the typed reason"
    );
}

void TestFullInMemoryRoundTrip() {
    using namespace custommodel;
    server::HandshakePolicy policy{};
    policy.serverCapabilities = protocol::kDeclaredCapabilities;
    policy.requiredCapabilities = Flag(protocol::Capability::Dff) |
        Flag(protocol::Capability::Txd);
    server::PlayerCapabilityTracker tracker{policy};
    FakeServerTransport transport;
    server::HandshakeEndpoint endpoint{tracker, transport};

    for (const auto version : {SampVersion::R1, SampVersion::R3_1}) {
        constexpr std::uint32_t playerId = 70;
        client_protocol::ClientHandshake client{{
            kRuntimeVersion,
            version,
            protocol::kDeclaredCapabilities,
        }};

        endpoint.OnPlayerConnected(playerId);
        client.OnConnected();
        const auto hello = client.TakeHello();
        Check(hello.has_value(), "loopback client should produce Hello");
        const auto helloFrame = hello.has_value()
            ? transport_frame::Encode({hello->data(), hello->size()})
            : std::nullopt;
        Check(helloFrame.has_value(), "loopback Hello should receive carrier framing");
        if (!helloFrame.has_value()) {
            continue;
        }

        Check(
            endpoint.DispatchIncoming(
                playerId,
                {helloFrame->data(), helloFrame->size()}
            ) == server::DispatchResult::WelcomeSent,
            "loopback server should send Welcome"
        );
        const auto response = transport_frame::Inspect(transport.sent);
        Check(
            response.kind == transport_frame::FrameKind::CustomModel,
            "loopback response should retain carrier framing"
        );
        Check(
            client.HandleIncoming(response.protocolBytes) ==
                client_protocol::IncomingResult::WelcomeReceived,
            "loopback client should consume Welcome"
        );
        Check(client.ConfirmWelcome(), "loopback Welcome should confirm");
        Check(
            client.State() == client_protocol::HandshakeState::Compatible,
            "full loopback handshake should end Compatible"
        );

        client.OnDisconnected();
        endpoint.OnPlayerDisconnected(playerId);
        Check(
            client.State() == client_protocol::HandshakeState::Disconnected &&
                tracker.GetState(playerId).state == server::HandshakeState::Unknown,
            "loopback disconnect should clear both endpoints"
        );
    }
}

}

int main() {
    TestCompatibleTransition();
    TestTypedRejections();
    TestCapabilityPolicy();
    TestTransportBuildPolicy();
    TestServerTransportBoundary();
    TestHandshakeEndpoint();
    TestDuplicateHelloAndReconnect();
    TestServerDispatchedPayloadBoundary();
    TestEndpointProtocolRejection();
    TestFullInMemoryRoundTrip();

    if (failures != 0) {
        std::cerr << failures << " server-state assertion(s) failed\n";
        return 1;
    }

    std::cout << "Server capability-state tests passed\n";
    return 0;
}
