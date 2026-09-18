#include <custommodel/assets/fetch.hpp>

#include <custommodel/assets.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>

#include <windows.h>
#include <winhttp.h>

namespace custommodel::client_assets {
namespace {

class InternetHandle final {
public:
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET handle) noexcept : handle_(handle) {}
    ~InternetHandle() {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
        }
    }

    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;

    HINTERNET Get() const noexcept {
        return handle_;
    }

    explicit operator bool() const noexcept {
        return handle_ != nullptr;
    }

private:
    HINTERNET handle_{};
};

FetchError MapWinHttpError(DWORD error) noexcept {
    return error == ERROR_WINHTTP_TIMEOUT
        ? FetchError::Timeout
        : FetchError::NetworkFailure;
}

std::string ErrorText(DWORD error) {
    std::array<char, 512> buffer{};
    auto length = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        nullptr
    );
    if (length == 0) {
        const auto module = GetModuleHandleW(L"winhttp.dll");
        if (module != nullptr) {
            length = FormatMessageA(
                FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS,
                module,
                error,
                0,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                nullptr
            );
        }
    }
    if (length == 0) {
        return "<unavailable>";
    }

    std::string text{buffer.data(), length};
    while (!text.empty() &&
           (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }
    return text.empty() ? "<unavailable>" : text;
}

struct ParsedUrl {
    std::wstring host;
    std::wstring target;
    std::wstring path;
    INTERNET_PORT port{};
    bool secure{};
};

std::string NarrowAscii(const std::wstring& value) {
    return {value.begin(), value.end()};
}

class WinHttpFetchBackend final : public FetchBackend {
public:
    WinHttpFetchBackend(
        FetchDiagnosticHandler diagnosticHandler,
        void* diagnosticContext
    ) noexcept : diagnosticHandler_(diagnosticHandler),
        diagnosticContext_(diagnosticContext) {}

    FetchResult Fetch(
        std::string_view url,
        const std::filesystem::path& destination,
        std::uint64_t expectedSize,
        const FetchOptions& options
    ) noexcept override {
        try {
            if (!assets::IsDownloadUrlValid(url) || expectedSize == 0 ||
                expectedSize > assets::kMaximumIndividualFileSize) {
                Log("WinHTTP URL or expected size rejected before parsing");
                return {FetchError::UnsupportedScheme, 0};
            }

            const auto parsed = ParseUrl(url);
            if (!parsed.has_value()) {
                return {FetchError::UnsupportedScheme, 0};
            }
            LogRequest(*parsed);

            InternetHandle session{WinHttpOpen(
                L"CustomModel/0.2",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            )};
            if (!session) {
                return Failure("WinHttpOpen", GetLastError());
            }

            const auto connectTimeout = static_cast<int>(std::min<std::uint32_t>(
                options.connectionTimeoutMilliseconds,
                static_cast<std::uint32_t>(std::numeric_limits<int>::max())
            ));
            if (!WinHttpSetTimeouts(
                    session.Get(),
                    connectTimeout,
                    connectTimeout,
                    connectTimeout,
                    connectTimeout
                )) {
                return Failure("WinHttpSetTimeouts", GetLastError());
            }

            DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
            if (!WinHttpSetOption(
                    session.Get(),
                    WINHTTP_OPTION_REDIRECT_POLICY,
                    &redirectPolicy,
                    sizeof(redirectPolicy)
                )) {
                return Failure(
                    "WinHttpSetOption(REDIRECT_POLICY)",
                    GetLastError()
                );
            }

            InternetHandle connection{WinHttpConnect(
                session.Get(),
                parsed->host.c_str(),
                parsed->port,
                0
            )};
            if (!connection) {
                return Failure("WinHttpConnect", GetLastError());
            }

            InternetHandle request{WinHttpOpenRequest(
                connection.Get(),
                L"GET",
                parsed->target.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                parsed->secure ? WINHTTP_FLAG_SECURE : 0
            )};
            if (!request) {
                return Failure("WinHttpOpenRequest", GetLastError());
            }

            DWORD redirects = options.redirectLimit;
            if (!WinHttpSetOption(
                    request.Get(),
                    WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS,
                    &redirects,
                    sizeof(redirects)
                )) {
                return Failure(
                    "WinHttpSetOption(MAX_HTTP_AUTOMATIC_REDIRECTS)",
                    GetLastError()
                );
            }

#ifdef WINHTTP_OPTION_REJECT_USERPWD_IN_URL
            DWORD rejectCredentials = TRUE;
            if (!WinHttpSetOption(
                    request.Get(),
                    WINHTTP_OPTION_REJECT_USERPWD_IN_URL,
                    &rejectCredentials,
                    sizeof(rejectCredentials)
                )) {
                LogFailure(
                    "WinHttpSetOption(REJECT_USERPWD_IN_URL, optional)",
                    GetLastError()
                );
            }
#endif

            if (!WinHttpSendRequest(
                    request.Get(),
                    WINHTTP_NO_ADDITIONAL_HEADERS,
                    0,
                    WINHTTP_NO_REQUEST_DATA,
                    0,
                    0,
                    0
                )) {
                return Failure("WinHttpSendRequest", GetLastError());
            }
            if (!WinHttpReceiveResponse(request.Get(), nullptr)) {
                return Failure("WinHttpReceiveResponse", GetLastError());
            }

            DWORD status{};
            DWORD statusSize = sizeof(status);
            if (!WinHttpQueryHeaders(
                    request.Get(),
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX,
                    &status,
                    &statusSize,
                    WINHTTP_NO_HEADER_INDEX
                )) {
                return Failure(
                    "WinHttpQueryHeaders(StatusCode)",
                    GetLastError()
                );
            }
            if (status != 200) {
                Log(std::string{"WinHTTP response rejected status="} +
                    std::to_string(status));
                return {FetchError::NetworkFailure, 0};
            }

            DWORD contentLength{};
            DWORD contentLengthSize = sizeof(contentLength);
            if (!WinHttpQueryHeaders(
                    request.Get(),
                    WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX,
                    &contentLength,
                    &contentLengthSize,
                    WINHTTP_NO_HEADER_INDEX
                )) {
                const auto error = GetLastError();
                LogFailure("WinHttpQueryHeaders(ContentLength)", error);
                if (error != ERROR_WINHTTP_HEADER_NOT_FOUND) {
                    return {MapWinHttpError(error), 0};
                }
            } else if (contentLength != expectedSize) {
                Log(std::string{"WinHTTP Content-Length mismatch received="} +
                    std::to_string(contentLength) + " expected=" +
                    std::to_string(expectedSize));
                return {FetchError::SizeMismatch, 0};
            }

            std::ofstream output(destination, std::ios::binary | std::ios::trunc);
            if (!output) {
                return {FetchError::DiskFailure, 0};
            }

            const auto started = GetTickCount64();
            std::array<char, 64 * 1024> buffer{};
            std::uint64_t received{};
            for (;;) {
                if (GetTickCount64() - started > options.transferTimeoutMilliseconds) {
                    Log("WinHTTP overall transfer timeout expired");
                    return {FetchError::Timeout, received};
                }
                DWORD available{};
                if (!WinHttpQueryDataAvailable(request.Get(), &available)) {
                    return Failure(
                        "WinHttpQueryDataAvailable",
                        GetLastError(),
                        received
                    );
                }
                if (available == 0) {
                    break;
                }
                if (available > expectedSize - received) {
                    return {FetchError::SizeMismatch, received};
                }

                DWORD bytesRead{};
                const auto requestSize = static_cast<DWORD>(std::min<std::size_t>(
                    available,
                    buffer.size()
                ));
                if (!WinHttpReadData(
                        request.Get(),
                        buffer.data(),
                        requestSize,
                        &bytesRead
                    )) {
                    return Failure("WinHttpReadData", GetLastError(), received);
                }
                if (bytesRead == 0 || bytesRead > expectedSize - received) {
                    return {FetchError::SizeMismatch, received};
                }
                output.write(buffer.data(), static_cast<std::streamsize>(bytesRead));
                if (!output) {
                    return {FetchError::DiskFailure, received};
                }
                received += bytesRead;
            }
            output.close();
            if (!output) {
                return {FetchError::DiskFailure, received};
            }
            return received == expectedSize
                ? FetchResult{FetchError::None, received}
                : FetchResult{FetchError::SizeMismatch, received};
        } catch (...) {
            return {FetchError::InternalError, 0};
        }
    }

private:
    std::optional<ParsedUrl> ParseUrl(std::string_view url) noexcept {
        try {
            const std::wstring wide(url.begin(), url.end());
            URL_COMPONENTS components{};
            components.dwStructSize = sizeof(components);
            components.dwSchemeLength = static_cast<DWORD>(-1);
            components.dwHostNameLength = static_cast<DWORD>(-1);
            components.dwUrlPathLength = static_cast<DWORD>(-1);
            components.dwExtraInfoLength = static_cast<DWORD>(-1);
            if (!WinHttpCrackUrl(
                    wide.c_str(),
                    static_cast<DWORD>(wide.size()),
                    0,
                    &components
                )) {
                LogFailure("WinHttpCrackUrl", GetLastError());
                return std::nullopt;
            }
            if (components.dwHostNameLength == 0 ||
                (components.nScheme != INTERNET_SCHEME_HTTP &&
                 components.nScheme != INTERNET_SCHEME_HTTPS)) {
                Log("WinHTTP WinHttpCrackUrl returned invalid URL components");
                return std::nullopt;
            }

            ParsedUrl result{};
            result.host.assign(components.lpszHostName, components.dwHostNameLength);
            if (components.dwUrlPathLength != 0) {
                result.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
            } else {
                result.path = L"/";
            }
            result.target = result.path;
            if (components.dwExtraInfoLength != 0) {
                result.target.append(
                    components.lpszExtraInfo,
                    components.dwExtraInfoLength
                );
            }
            result.port = components.nPort;
            result.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
            return result;
        } catch (...) {
            Log("WinHTTP URL parsing failed internally");
            return std::nullopt;
        }
    }

    FetchResult Failure(
        const char* operation,
        DWORD error,
        std::uint64_t received = 0
    ) noexcept {
        LogFailure(operation, error);
        return {MapWinHttpError(error), received};
    }

    void LogFailure(const char* operation, DWORD error) noexcept {
        try {
            Log(std::string{"WinHTTP "} + operation + " failed error=" +
                std::to_string(error) + " text=\"" + ErrorText(error) + '"');
        } catch (...) {
        }
    }

    void LogRequest(const ParsedUrl& parsed) noexcept {
        try {
            Log(std::string{"WinHTTP request scheme="} +
                (parsed.secure ? "https" : "http") + " host=" +
                NarrowAscii(parsed.host) + " port=" +
                std::to_string(parsed.port) + " path=" +
                NarrowAscii(parsed.path) + " secure=" +
                (parsed.secure ? "true" : "false"));
        } catch (...) {
        }
    }

    void Log(std::string_view message) noexcept {
        if (diagnosticHandler_ != nullptr) {
            diagnosticHandler_(message, diagnosticContext_);
        }
    }

    FetchDiagnosticHandler diagnosticHandler_{};
    void* diagnosticContext_{};
};

}

std::unique_ptr<FetchBackend> CreateWinHttpFetchBackend(
    FetchDiagnosticHandler diagnosticHandler,
    void* diagnosticContext
) {
    return std::make_unique<WinHttpFetchBackend>(
        diagnosticHandler,
        diagnosticContext
    );
}

}
