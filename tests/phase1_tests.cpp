#include <custommodel/pe_image.hpp>
#include <custommodel/samp/compatibility.hpp>
#include <custommodel/version.hpp>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename T>
void Write(std::vector<std::uint8_t>& image, std::size_t offset, T value) {
    std::memcpy(image.data() + offset, &value, sizeof(value));
}

struct PeFixture {
    std::uint16_t optionalMagic{};
    std::uint32_t entryPoint{};
    std::uint32_t imageSize{};
};

std::vector<std::uint8_t> MakePeImage(PeFixture fixture) {
    constexpr std::size_t ntOffset = 0x80;
    constexpr std::size_t coffOffset = ntOffset + 4;
    constexpr std::size_t optionalOffset = coffOffset + 20;

    std::vector<std::uint8_t> image(0x200);
    Write<std::uint16_t>(image, 0x00, 0x5A4D);
    Write<std::uint32_t>(image, 0x3C, static_cast<std::uint32_t>(ntOffset));
    Write<std::uint32_t>(image, ntOffset, 0x00004550);
    Write<std::uint16_t>(image, coffOffset + 16, 0xE0);
    Write<std::uint16_t>(image, optionalOffset, fixture.optionalMagic);
    Write<std::uint32_t>(image, optionalOffset + 16, fixture.entryPoint);
    Write<std::uint32_t>(image, optionalOffset + 56, fixture.imageSize);
    return image;
}

void TestPe32Parsing() {
    const auto bytes = MakePeImage({0x010B, 0x31DF13, 0x400000});
    const auto parsed = custommodel::ParsePeImage(bytes.data(), bytes.size());

    Check(static_cast<bool>(parsed), "valid PE32 image should parse");
    if (parsed.image.has_value()) {
        const auto& image = *parsed.image;
        Check(image.kind == custommodel::PeImageKind::Pe32, "PE32 kind should be retained");
        Check(image.entryPointRva == 0x31DF13, "entry-point RVA should be read");
        Check(image.sizeOfImage == 0x400000, "image size should be read");
    }
}

void TestPe32PlusParsing() {
    const auto bytes = MakePeImage({0x020B, 0x1234, 0x500000});
    const auto parsed = custommodel::ParsePeImage(bytes.data(), bytes.size());

    Check(static_cast<bool>(parsed), "valid PE32+ image should parse for safe rejection by the client");
    if (parsed.image.has_value()) {
        Check(
            parsed.image->kind == custommodel::PeImageKind::Pe32Plus,
            "PE32+ kind should be retained"
        );
    }
}

void TestMalformedImages() {
    std::vector<std::uint8_t> truncated(16);
    auto parsed = custommodel::ParsePeImage(truncated.data(), truncated.size());
    Check(
        parsed.error == custommodel::PeParseError::TruncatedDosHeader,
        "truncated DOS header should fail"
    );

    auto invalidDos = MakePeImage({0x010B, 1, 0x1000});
    Write<std::uint16_t>(invalidDos, 0, 0);
    parsed = custommodel::ParsePeImage(invalidDos.data(), invalidDos.size());
    Check(
        parsed.error == custommodel::PeParseError::InvalidDosSignature,
        "invalid DOS signature should fail"
    );

    auto invalidOptional = MakePeImage({0x9999, 1, 0x1000});
    parsed = custommodel::ParsePeImage(invalidOptional.data(), invalidOptional.size());
    Check(
        parsed.error == custommodel::PeParseError::UnsupportedOptionalHeader,
        "unknown optional-header magic should fail"
    );

    auto zeroImageSize = MakePeImage({0x010B, 1, 0});
    parsed = custommodel::ParsePeImage(zeroImageSize.data(), zeroImageSize.size());
    Check(
        parsed.error == custommodel::PeParseError::InvalidImageSize,
        "zero image size should fail"
    );
}

void TestBuildRegistry() {
    const auto* r1 = custommodel::FindSampBuild(0x31DF13);
    const auto* r2 = custommodel::FindSampBuild(0x3195DD);
    const auto* r31 = custommodel::FindSampBuild(0x0CC4D0);
    const auto* r4Reference = custommodel::FindSampBuild(0x0CBCB0);
    const auto* r4Family = custommodel::FindSampBuild(0x0CBCD0);

    Check(r1 != nullptr && r1->version == custommodel::SampVersion::R1, "R1 should be recognized");
    Check(r2 != nullptr && r2->version == custommodel::SampVersion::R2, "R2 should be recognized");
    if (r2 != nullptr) {
        Check(
            std::string_view{r2->name} == "SA-MP 0.3.7 R2",
            "R2 should use the runtime log label confirmed by testing"
        );
    }
    Check(
        r31 != nullptr && r31->version == custommodel::SampVersion::R3_1,
        "R3-1 should be recognized"
    );
    Check(
        r4Reference != nullptr && r4Reference->version == custommodel::SampVersion::R4,
        "the existing public-reference R4 entry point should remain recognized"
    );
    Check(
        r4Family != nullptr && r4Family->version == custommodel::SampVersion::R4Family,
        "the runtime-observed R4-family entry point should be recognized separately"
    );
    if (r4Family != nullptr) {
        Check(
            std::string_view{r4Family->name} == "SA-MP 0.3.7 R4-family",
            "the observed R4-family label should not claim an exact minor revision"
        );
    }
    Check(custommodel::FindSampBuild(0xDEADBEEF) == nullptr, "unknown builds should be rejected");
    Check(custommodel::KnownSampBuilds().size() == 5, "all five documented entry points should be registered");
}

void TestCompatibilityFailsClosed() {
    const auto* r1 = custommodel::samp::FindCompatibility(custommodel::SampVersion::R1);
    Check(r1 != nullptr, "recognized R1 should have a compatibility record");
    if (r1 != nullptr) {
        Check(
            r1->addresses.netGamePointerRva == 0x21A0F8,
            "publicly verified R1 CNetGame pointer RVA should be present"
        );
        Check(
            r1->addresses.getRakClientRva == 0x1A40,
            "publicly verified R1 GetRakClient RVA should be present"
        );
        Check(
            r1->HasAllMappings({
                custommodel::samp::AddressMapping::NetGame,
                custommodel::samp::AddressMapping::GetRakClient,
            }),
            "publicly verified R1 RakClient resolution path should be complete"
        );
        Check(
            !r1->HasMapping(custommodel::samp::AddressMapping::RpcHandler),
            "R1 RPC mapping should remain unresolved"
        );
    }

    const auto* r31 = custommodel::samp::FindCompatibility(custommodel::SampVersion::R3_1);
    Check(r31 != nullptr, "recognized R3-1 should have a compatibility record");
    if (r31 != nullptr) {
        Check(
            r31->addresses.netGamePointerRva == 0x26E8DC,
            "publicly verified R3-1 CNetGame pointer RVA should be present"
        );
        Check(
            r31->addresses.getRakClientRva == 0x1A40,
            "publicly verified R3-1 GetRakClient RVA should be present"
        );
        Check(
            !r31->HasMapping(custommodel::samp::AddressMapping::RpcHandler),
            "R3-1 RPC mapping should remain unresolved"
        );
    }

    const auto* r2 = custommodel::samp::FindCompatibility(custommodel::SampVersion::R2);
    Check(r2 != nullptr, "recognized R2 should have a compatibility record");
    if (r2 != nullptr) {
        Check(
            r2->addresses.netGamePointerRva == 0 &&
                r2->addresses.getRakClientRva == 0 &&
                r2->addresses.rpcHandlerRva == 0,
            "R2 internal mappings should remain unresolved"
        );
    }

    const auto* r4 = custommodel::samp::FindCompatibility(custommodel::SampVersion::R4);
    Check(r4 != nullptr, "recognized R4 record should have a compatibility record");
    if (r4 != nullptr) {
        Check(
            r4->addresses.netGamePointerRva == 0 &&
                r4->addresses.getRakClientRva == 0 &&
                r4->addresses.rpcHandlerRva == 0,
            "public-reference R4 internal mappings should remain unresolved"
        );
    }

    const auto* r4Family = custommodel::samp::FindCompatibility(
        custommodel::SampVersion::R4Family
    );
    Check(r4Family != nullptr, "recognized R4-family should have a compatibility record");
    if (r4Family != nullptr) {
        Check(
            r4Family->addresses.netGamePointerRva == 0 &&
                r4Family->addresses.getRakClientRva == 0 &&
                r4Family->addresses.rpcHandlerRva == 0,
            "R4-family internal mappings should remain unresolved"
        );
    }

    Check(
        custommodel::samp::FindCompatibility(custommodel::SampVersion::Unknown) == nullptr,
        "unknown versions should not receive a compatibility record"
    );
}

}

int main() {
    TestPe32Parsing();
    TestPe32PlusParsing();
    TestMalformedImages();
    TestBuildRegistry();
    TestCompatibilityFailsClosed();

    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return 1;
    }

    std::cout << "Phase 1 tests passed\n";
    return 0;
}
