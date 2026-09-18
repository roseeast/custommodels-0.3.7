#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

namespace custommodel::client_assets {

using FetchDiagnosticHandler = void(*)(std::string_view message, void* context) noexcept;

enum class FetchError : std::uint8_t {
    None,
    UnsupportedScheme,
    NetworkFailure,
    Timeout,
    SizeMismatch,
    DiskFailure,
    InternalError,
};

struct FetchOptions {
    std::uint32_t redirectLimit{5};
    std::uint32_t connectionTimeoutMilliseconds{10000};
    std::uint32_t transferTimeoutMilliseconds{120000};
};

struct FetchResult {
    FetchError error{FetchError::None};
    std::uint64_t bytesReceived{};

    explicit operator bool() const noexcept {
        return error == FetchError::None;
    }
};

class FetchBackend {
public:
    virtual ~FetchBackend() = default;
    virtual FetchResult Fetch(
        std::string_view url,
        const std::filesystem::path& destination,
        std::uint64_t expectedSize,
        const FetchOptions& options
    ) noexcept = 0;
};

#if defined(_WIN32)
std::unique_ptr<FetchBackend> CreateWinHttpFetchBackend(
    FetchDiagnosticHandler diagnosticHandler = nullptr,
    void* diagnosticContext = nullptr
);
#endif

}
