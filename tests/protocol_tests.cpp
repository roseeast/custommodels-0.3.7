#include <custommodel/protocol.hpp>
#include <custommodel/protocol/handshake.hpp>
#include <custommodel/protocol/transport.hpp>
#include <custommodel/samp/raknet_transport.hpp>
#include <custommodel/transport_frame.hpp>

#include <algorithm>
#include <array>
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

void PutU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 1] = static_cast<std::uint8_t>(value);
}

void PutU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 3] = static_cast<std::uint8_t>(value);
}

custommodel::protocol::CapabilityFlags Capabilities() {
    using custommodel::protocol::Capability;
    return static_cast<std::uint32_t>(Capability::CharModel) |
        static_cast<std::uint32_t>(Capability::Dff) |
        static_cast<std::uint32_t>(Capability::Sha256Cache);
}

custommodel::protocol::Hello MakeHello() {
    return {
        {1, 2, 513},
        custommodel::protocol::kProtocolVersion,
        custommodel::SampVersion::R3_1,
        Capabilities(),
    };
}

custommodel::assets::AssetDescription MakeAsset(std::uint32_t assetId = 1001) {
    custommodel::assets::Sha256Digest digest{};
    for (std::size_t index = 0; index < digest.size(); ++index) {
        digest[index] = static_cast<std::uint8_t>(index);
    }
    return {
        assetId,
        custommodel::assets::AssetType::Dff,
        "models/test_asset",
        "https://assets.example.test/test_asset.dff",
        1234,
        digest,
    };
}

void TestHello() {
    using namespace custommodel::protocol;
    const auto encoded = EncodePacket(MakeHello());
    Check(static_cast<bool>(encoded), "Hello should encode");
    Check(encoded.bytes.size() == kHeaderSize + 14, "Hello should have a fixed payload size");
    if (encoded.bytes.size() >= kHeaderSize + 14) {
        Check(
            encoded.bytes[0] == 'C' && encoded.bytes[1] == 'M' &&
                encoded.bytes[2] == 'O' && encoded.bytes[3] == 'D',
            "wire magic should be CMOD"
        );
        Check(
            encoded.bytes[16] == 0x02 && encoded.bytes[17] == 0x01,
            "integers should use big-endian byte order"
        );
    }

    const auto decoded = DecodePacket(encoded.bytes);
    Check(static_cast<bool>(decoded), "encoded Hello should decode");
    const auto* hello = decoded.packet.has_value()
        ? std::get_if<Hello>(&decoded.packet->message)
        : nullptr;
    Check(hello != nullptr, "decoded message should be Hello");
    if (hello != nullptr) {
        Check(hello->runtimeVersion.major == 1, "Hello runtime major should round trip");
        Check(hello->runtimeVersion.minor == 2, "Hello runtime minor should round trip");
        Check(hello->runtimeVersion.patch == 513, "Hello runtime patch should round trip");
        Check(hello->protocolVersion == kProtocolVersion, "Hello protocol should round trip");
        Check(hello->sampVersion == custommodel::SampVersion::R3_1, "SA-MP build should round trip");
        Check(hello->capabilities == Capabilities(), "Hello capabilities should round trip");
    }
}

void TestWelcome() {
    using namespace custommodel::protocol;
    const Welcome original{
        kProtocolVersion,
        {3, 4, 5},
        kProtocolVersion,
        Capabilities(),
    };
    const auto decoded = DecodePacket(EncodePacket(original).bytes);
    const auto* welcome = decoded.packet.has_value()
        ? std::get_if<Welcome>(&decoded.packet->message)
        : nullptr;
    Check(static_cast<bool>(decoded) && welcome != nullptr, "Welcome should round trip");
    if (welcome != nullptr) {
        Check(welcome->acceptedProtocolVersion == kProtocolVersion, "accepted protocol should round trip");
        Check(welcome->serverRuntimeVersion.major == 3, "server runtime should round trip");
        Check(welcome->serverRuntimeVersion.minor == 4, "server runtime minor should round trip");
        Check(welcome->serverRuntimeVersion.patch == 5, "server runtime patch should round trip");
        Check(welcome->serverProtocolVersion == kProtocolVersion, "server protocol should round trip");
        Check(welcome->negotiatedCapabilities == Capabilities(), "negotiated capabilities should round trip");
    }
}

void TestReject() {
    using namespace custommodel::protocol;
    const Reject reasons[]{
        {RejectReason::UnsupportedProtocol},
        {RejectReason::UnsupportedClientRuntime},
        {RejectReason::UnsupportedSampBuild},
        {RejectReason::MalformedHandshake},
        {RejectReason::UnsupportedCapabilities},
    };
    for (const auto reason : reasons) {
        const auto decoded = DecodePacket(EncodePacket(reason).bytes);
        const auto* reject = decoded.packet.has_value()
            ? std::get_if<Reject>(&decoded.packet->message)
            : nullptr;
        Check(static_cast<bool>(decoded) && reject != nullptr, "Reject should round trip");
        if (reject != nullptr) {
            Check(reject->reason == reason.reason, "typed Reject reason should round trip");
        }
    }
}

void TestPingPong() {
    using namespace custommodel::protocol;
    constexpr std::uint64_t nonce = 0x0123456789ABCDEFULL;
    const auto ping = DecodePacket(EncodePacket(Ping{nonce}).bytes);
    const auto pong = DecodePacket(EncodePacket(Pong{nonce}).bytes);
    const auto* decodedPing = ping.packet.has_value()
        ? std::get_if<Ping>(&ping.packet->message)
        : nullptr;
    const auto* decodedPong = pong.packet.has_value()
        ? std::get_if<Pong>(&pong.packet->message)
        : nullptr;
    Check(decodedPing != nullptr && decodedPing->nonce == nonce, "Ping nonce should round trip");
    Check(decodedPong != nullptr && decodedPong->nonce == nonce, "Pong nonce should round trip");
}

void TestManifestMessages() {
    using namespace custommodel::protocol;
    constexpr std::uint64_t revision = 0x0102030405060708ULL;
    const auto asset = MakeAsset();
    const Message messages[]{
        ManifestBegin{revision, 1, asset.fileSize},
        ManifestAsset{revision, 0, asset},
        ManifestEnd{revision, 1, asset.fileSize},
        AssetReady{revision, asset.assetId},
        AssetError{revision, asset.assetId, AssetErrorReason::HashMismatch},
    };
    for (const auto& message : messages) {
        const auto encoded = EncodePacket(message);
        const auto decoded = DecodePacket(encoded.bytes);
        Check(static_cast<bool>(encoded) && static_cast<bool>(decoded),
            "every Phase 3 message should round trip");
    }

    const auto decodedAsset = DecodePacket(
        EncodePacket(ManifestAsset{revision, 0, asset}).bytes
    );
    const auto* item = decodedAsset.packet.has_value()
        ? std::get_if<ManifestAsset>(&decodedAsset.packet->message)
        : nullptr;
    Check(item != nullptr && item->asset.assetId == asset.assetId &&
        item->asset.sha256 == asset.sha256 && item->asset.downloadUrl == asset.downloadUrl,
        "manifest asset fields should round trip exactly");
}

void TestManifestMessageValidation() {
    using namespace custommodel::protocol;
    constexpr std::uint64_t revision = 9;
    const auto asset = MakeAsset();
    const auto validAsset = EncodePacket(ManifestAsset{revision, 0, asset}).bytes;

    auto unknownType = validAsset;
    PutU16(unknownType, kHeaderSize + 8 + 4 + 4, 0x7FFF);
    Check(
        DecodePacket(unknownType).error == DecodeError::InvalidField,
        "unknown asset types should be rejected"
    );

    auto oversizedName = validAsset;
    PutU16(
        oversizedName,
        kHeaderSize + 8 + 4 + 4 + 2,
        static_cast<std::uint16_t>(custommodel::assets::kMaximumLogicalNameLength + 1)
    );
    Check(!DecodePacket(oversizedName), "oversized logical names should be rejected");

    auto truncatedHash = validAsset;
    truncatedHash.pop_back();
    PutU32(
        truncatedHash,
        8,
        static_cast<std::uint32_t>(truncatedHash.size() - kHeaderSize)
    );
    Check(
        DecodePacket(truncatedHash).error == DecodeError::TruncatedPayload,
        "truncated SHA-256 bytes should be rejected"
    );

    auto unknownReason = EncodePacket(AssetError{
        revision,
        asset.assetId,
        AssetErrorReason::HashMismatch,
    }).bytes;
    PutU16(unknownReason, kHeaderSize + 8 + 4, 0xFFFF);
    Check(
        DecodePacket(unknownReason).error == DecodeError::InvalidField,
        "unknown asset error reasons should be rejected"
    );

    auto trailing = validAsset;
    trailing.push_back(0);
    Check(
        DecodePacket(trailing).error == DecodeError::PayloadLengthMismatch,
        "trailing manifest bytes should be rejected"
    );

    auto invalidAsset = asset;
    invalidAsset.downloadUrl = "file:///tmp/model.dff";
    Check(
        EncodePacket(ManifestAsset{revision, 0, invalidAsset}).error ==
            EncodeError::InvalidField,
        "unsupported download schemes should not be serialized"
    );
}

void TestHeaderValidation() {
    using namespace custommodel::protocol;
    const auto valid = EncodePacket(MakeHello()).bytes;

    auto invalidMagic = valid;
    invalidMagic[0] ^= 0xFF;
    Check(
        DecodePacket(invalidMagic).error == DecodeError::InvalidMagic,
        "invalid magic should be rejected"
    );

    const std::vector<std::uint8_t> shortHeader(kHeaderSize - 1);
    Check(
        DecodePacket(shortHeader).error == DecodeError::TruncatedHeader,
        "truncated header should be rejected"
    );

    auto invalidProtocol = valid;
    PutU16(invalidProtocol, 4, kProtocolVersion + 1);
    Check(
        DecodePacket(invalidProtocol).error == DecodeError::UnsupportedProtocolVersion,
        "unsupported header protocol should be rejected"
    );
    Check(
        EncodePacket(MakeHello(), kProtocolVersion + 1).error == EncodeError::InvalidField,
        "encoder should not emit an unsupported header protocol"
    );

    auto unknownMessage = valid;
    PutU16(unknownMessage, 6, 0x7FFF);
    Check(
        DecodePacket(unknownMessage).error == DecodeError::UnknownMessageType,
        "unknown message ID should be rejected"
    );
}

void TestPayloadValidation() {
    using namespace custommodel::protocol;
    const auto valid = EncodePacket(MakeHello()).bytes;

    auto truncated = valid;
    truncated.pop_back();
    Check(
        DecodePacket(truncated).error == DecodeError::TruncatedPayload,
        "truncated payload should be rejected"
    );

    auto lengthMismatch = valid;
    PutU32(lengthMismatch, 8, 13);
    Check(
        DecodePacket(lengthMismatch).error == DecodeError::PayloadLengthMismatch,
        "incorrect payload length should be rejected"
    );

    auto oversized = valid;
    PutU32(oversized, 8, static_cast<std::uint32_t>(kMaximumPayloadSize + 1));
    Check(
        DecodePacket(oversized).error == DecodeError::OversizedPayload,
        "oversized payload should be rejected before allocation"
    );

    auto invalidSampVersion = valid;
    PutU16(invalidSampVersion, kHeaderSize + 8, 0xFFFF);
    Check(
        DecodePacket(invalidSampVersion).error == DecodeError::InvalidField,
        "invalid SA-MP build integer should be rejected"
    );

    auto invalidCapabilities = valid;
    PutU32(invalidCapabilities, kHeaderSize + 10, 0x80000000U);
    Check(
        DecodePacket(invalidCapabilities).error == DecodeError::InvalidField,
        "unknown capability bits should be rejected in protocol version 1"
    );

    auto invalidReject = EncodePacket(Reject{RejectReason::MalformedHandshake}).bytes;
    PutU16(invalidReject, kHeaderSize, 0);
    Check(
        DecodePacket(invalidReject).error == DecodeError::InvalidField,
        "invalid Reject reason integer should be rejected"
    );

    Check(
        std::string_view{ToString(RejectReason::UnsupportedCapabilities)} ==
            "unsupported capabilities",
        "Reject reasons should have bounded diagnostic text"
    );
}

void TestCapabilities() {
    using namespace custommodel::protocol;
    const auto flags = Capabilities();
    Check(HasCapability(flags, Capability::CharModel), "CHAR_MODEL flag should be testable");
    Check(!HasCapability(flags, Capability::ObjectModel), "unset OBJECT_MODEL flag should remain clear");
    Check(HasCapability(flags, Capability::Dff), "DFF flag should be testable");
    Check(HasCapability(flags, Capability::Sha256Cache), "SHA256_CACHE flag should be testable");
    Check(AreCapabilitiesValid(kDeclaredCapabilities), "all declared capability flags should be valid");
    Check(!AreCapabilitiesValid(1U << 31U), "undeclared capability flags should be invalid");

    auto malformed = MakeHello();
    malformed.capabilities = 1U << 31U;
    Check(
        EncodePacket(malformed).error == EncodeError::InvalidField,
        "encoder should reject malformed capability fields"
    );
}

void TestTransportsFailClosed() {
    for (const auto& build : custommodel::KnownSampBuilds()) {
        auto& transport = custommodel::client_protocol::SelectSampTransport(build.version);
        Check(!transport.IsAvailable(), "unverified SA-MP transport should be unavailable");
        Check(!transport.Initialize({}), "unverified transport should not initialize");
        Check(!transport.Send({nullptr, 0}), "unverified transport should not send");
        Check(!transport.DispatchIncoming({nullptr, 0}), "unverified transport should not dispatch");
        transport.Shutdown();
    }
}

void TestTransportFraming() {
    using namespace custommodel;
    const auto protocolPacket = protocol::EncodePacket(MakeHello()).bytes;
    const auto frame = transport_frame::Encode({protocolPacket.data(), protocolPacket.size()});
    Check(frame.has_value(), "a CMOD packet should fit in a RakNet transport frame");
    if (frame.has_value()) {
        Check(
            frame->size() >= 5 &&
                (*frame)[0] == 0x5E &&
                (*frame)[1] == 0x43 &&
                (*frame)[2] == 0x4D &&
                (*frame)[3] == 0x4F &&
                (*frame)[4] == 0x44,
            "a real Hello transport frame should start with 5E 43 4D 4F 44"
        );
        const auto inspected = transport_frame::Inspect(*frame);
        Check(
            inspected.kind == transport_frame::FrameKind::CustomModel,
            "carrier plus CMOD magic should identify a CustomModel frame"
        );
        Check(
            inspected.protocolBytes.size == protocolPacket.size(),
            "frame inspection should remove only the carrier byte"
        );
        const auto dispatchedPayload = transport_frame::InspectProtocolPayload({
            frame->data() + transport_frame::kPrefixSize,
            frame->size() - transport_frame::kPrefixSize,
        });
        Check(
            dispatchedPayload.kind == transport_frame::FrameKind::CustomModel &&
                dispatchedPayload.protocolBytes.data == frame->data() + 1,
            "the server-dispatched CMOD payload should not require the carrier byte"
        );
    }

    const std::vector<std::uint8_t> ordinaryPacket{200, 1, 2, 3};
    Check(
        transport_frame::Inspect(ordinaryPacket).kind ==
            transport_frame::FrameKind::Unrelated,
        "ordinary SA-MP packets should remain unrelated"
    );

    const std::vector<std::uint8_t> otherUserPacket{
        transport_frame::kRakNetPacketId, 'N', 'O', 'P', 'E', 1,
    };
    Check(
        transport_frame::Inspect(otherUserPacket).kind ==
            transport_frame::FrameKind::Unrelated,
        "the shared RakNet user ID without CMOD magic should remain unrelated"
    );

    const std::vector<std::uint8_t> shortUserPacket{
        transport_frame::kRakNetPacketId, 'C', 'M',
    };
    Check(
        transport_frame::Inspect(shortUserPacket).kind ==
            transport_frame::FrameKind::Unrelated,
        "a partial magic prefix should not claim another user's packet"
    );
    Check(
        transport_frame::InspectProtocolPayload({
            shortUserPacket.data() + 1,
            shortUserPacket.size() - 1,
        }).kind == transport_frame::FrameKind::Unrelated,
        "a truncated dispatched CMOD prefix should remain unrelated"
    );

    const std::vector<std::uint8_t> wrongDispatchedMagic{'N', 'O', 'P', 'E'};
    Check(
        transport_frame::InspectProtocolPayload({
            wrongDispatchedMagic.data(),
            wrongDispatchedMagic.size(),
        }).kind ==
            transport_frame::FrameKind::Unrelated,
        "a dispatched payload with wrong magic should remain unrelated"
    );

    const std::vector<std::uint8_t> malformedCustomModel{
        transport_frame::kRakNetPacketId, 'C', 'M', 'O', 'D',
    };
    const auto malformedFrame = transport_frame::Inspect(malformedCustomModel);
    Check(
        malformedFrame.kind == transport_frame::FrameKind::CustomModel,
        "full carrier and magic should claim malformed CMOD data for safe dropping"
    );
    Check(
        protocol::DecodePacket(malformedFrame.protocolBytes).error ==
            protocol::DecodeError::TruncatedHeader,
        "claimed malformed CMOD framing should fail bounded protocol decoding"
    );
}

void TestRuntimeHelloRawSendBoundary() {
    using namespace custommodel;
    struct FakeRawSendBoundary {
        const char* data{};
        int byteLength{};

        void Send(samp::RakNetRawSendBuffer arguments) noexcept {
            data = arguments.data;
            byteLength = arguments.byteLength;
        }
    } fakeRawSend;

    const protocol::Hello hello{
        kRuntimeVersion,
        protocol::kProtocolVersion,
        SampVersion::R1,
        protocol::kDeclaredCapabilities,
    };
    const auto encoded = protocol::EncodePacket(hello);
    Check(static_cast<bool>(encoded), "the runtime protocol v1 Hello should encode");
    Check(encoded.bytes.size() == 26, "the runtime CMOD Hello should be exactly 26 bytes");

    const auto frame = transport_frame::Encode({encoded.bytes.data(), encoded.bytes.size()});
    Check(frame.has_value(), "the runtime Hello should receive transport framing");
    if (!frame.has_value()) {
        return;
    }

    Check(frame->size() == 27, "the framed runtime Hello should be exactly 27 bytes");
    constexpr std::array<std::uint8_t, 13> expectedPrefix{
        0x5E, 0x43, 0x4D, 0x4F, 0x44, 0x00, 0x01,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x0E,
    };
    Check(
        frame->size() >= expectedPrefix.size() &&
            std::equal(expectedPrefix.begin(), expectedPrefix.end(), frame->begin()),
        "the runtime Hello should have the exact carrier and CMOD header bytes"
    );

    const auto raw = samp::MakeRakNetRawSendBuffer({frame->data(), frame->size()});
    Check(static_cast<bool>(raw), "the framed Hello should produce raw-send arguments");
    Check(
        raw.data == reinterpret_cast<const char*>(frame->data()),
        "the raw-send boundary should retain the exact framed data pointer"
    );
    Check(raw.byteLength == 27, "the raw RakNet Send length should be exactly 27 bytes");
    fakeRawSend.Send(raw);
    Check(
        fakeRawSend.data == reinterpret_cast<const char*>(frame->data()) &&
            fakeRawSend.byteLength == 27,
        "a raw-send call should receive only the 27 serialized frame bytes"
    );
}

void TestClientHandshakeState() {
    using namespace custommodel;
    client_protocol::ClientHandshake handshake{{
        kRuntimeVersion,
        SampVersion::R1,
        Capabilities(),
    }};

    Check(
        handshake.State() == client_protocol::HandshakeState::Disconnected,
        "client handshake should start disconnected"
    );
    Check(!handshake.TakeHello().has_value(), "Hello should not be available while disconnected");

    handshake.OnConnected();
    const auto hello = handshake.TakeHello();
    Check(hello.has_value(), "connection should make one Hello available");
    Check(!handshake.TakeHello().has_value(), "Hello should be emitted only once per connection");

    const auto welcome = protocol::EncodePacket(protocol::Welcome{
        protocol::kProtocolVersion,
        kRuntimeVersion,
        protocol::kProtocolVersion,
        Capabilities(),
    });
    Check(
        handshake.HandleIncoming({welcome.bytes.data(), welcome.bytes.size()}) ==
            client_protocol::IncomingResult::WelcomeReceived,
        "Welcome should be accepted after Hello"
    );
    Check(
        handshake.State() == client_protocol::HandshakeState::WelcomeReceived,
        "Welcome should have an explicit intermediate state"
    );
    Check(handshake.ConfirmWelcome(), "validated Welcome should complete the handshake");
    Check(
        handshake.State() == client_protocol::HandshakeState::Compatible,
        "confirmed Welcome should transition to Compatible"
    );

    handshake.OnDisconnected();
    handshake.OnConnected();
    Check(handshake.TakeHello().has_value(), "reconnect should reset the one-Hello guard");

    auto malformed = welcome.bytes;
    malformed.pop_back();
    const auto beforeMalformed = handshake.State();
    Check(
        handshake.HandleIncoming({malformed.data(), malformed.size()}) ==
            client_protocol::IncomingResult::Malformed,
        "truncated incoming handshake data should be malformed"
    );
    Check(
        handshake.State() == beforeMalformed,
        "malformed incoming data must not transition client state"
    );

    const auto reject = protocol::EncodePacket(protocol::Reject{
        protocol::RejectReason::UnsupportedSampBuild,
    });
    Check(
        handshake.HandleIncoming({reject.bytes.data(), reject.bytes.size()}) ==
            client_protocol::IncomingResult::Rejected,
        "Reject should be handled after Hello"
    );
    Check(
        handshake.State() == client_protocol::HandshakeState::Rejected,
        "Reject should transition the client to Rejected"
    );
}

}

int main() {
    TestHello();
    TestWelcome();
    TestReject();
    TestPingPong();
    TestManifestMessages();
    TestManifestMessageValidation();
    TestHeaderValidation();
    TestPayloadValidation();
    TestCapabilities();
    TestTransportFraming();
    TestRuntimeHelloRawSendBoundary();
    TestClientHandshakeState();
    TestTransportsFailClosed();

    if (failures != 0) {
        std::cerr << failures << " protocol assertion(s) failed\n";
        return 1;
    }

    std::cout << "Protocol tests passed\n";
    return 0;
}
