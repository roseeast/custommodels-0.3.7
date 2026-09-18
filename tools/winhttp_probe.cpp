#include <custommodel/assets/fetch.hpp>
#include <custommodel/sha256.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void PrintDiagnostic(std::string_view message, void*) noexcept {
    try {
        std::cout << "[CustomModel:debug] " << message << '\n';
    } catch (...) {
    }
}

}

int main(int argc, char** argv) {
    const std::string url = argc > 1
        ? argv[1]
        : "http://127.0.0.1:8000/test_asset.dff";
    const std::filesystem::path destination = argc > 2
        ? std::filesystem::path{argv[2]}
        : std::filesystem::path{"winhttp-probe.tmp"};
    std::uint64_t expectedSize{40};
    if (argc > 3) {
        try {
            expectedSize = std::stoull(argv[3]);
        } catch (...) {
            std::cerr << "invalid expected size\n";
            return 2;
        }
    }

    auto backend = custommodel::client_assets::CreateWinHttpFetchBackend(
        &PrintDiagnostic,
        nullptr
    );
    if (backend == nullptr) {
        std::cerr << "could not create WinHTTP backend\n";
        return 2;
    }

    const auto result = backend->Fetch(
        url,
        destination,
        expectedSize,
        custommodel::client_assets::FetchOptions{}
    );
    if (!result) {
        std::cerr << "fetch failed code=" << static_cast<unsigned>(result.error)
                  << " bytes=" << result.bytesReceived << '\n';
        return 1;
    }

    const auto digest = custommodel::assets::Sha256File(destination);
    if (!digest.has_value()) {
        std::cerr << "downloaded file could not be hashed\n";
        return 1;
    }
    std::cout << "fetch succeeded bytes=" << result.bytesReceived
              << " sha256=" << custommodel::assets::Sha256Hex(*digest) << '\n';
    return 0;
}
