#include <custommodel/core/logger.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>

namespace custommodel::core {
namespace {

Logger g_logger;

std::filesystem::path LogPath(HMODULE module) {
    std::array<wchar_t, 32768> buffer{};
    const auto length = GetModuleFileNameW(
        module,
        buffer.data(),
        static_cast<DWORD>(buffer.size())
    );
    if (length == 0 || length == buffer.size()) {
        return {};
    }

    const std::filesystem::path modulePath{std::wstring(buffer.data(), length)};
    return modulePath.parent_path() / L"CustomModel" / L"logs" / L"custommodel.log";
}

}

bool Logger::Open(HMODULE module) noexcept {
    try {
        const auto path = LogPath(module);
        if (path.empty()) {
            OutputDebugStringA("[CustomModel] could not resolve the ASI path\n");
            return false;
        }

        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            OutputDebugStringA("[CustomModel] could not create the log directory\n");
            return false;
        }

        AcquireSRWLockExclusive(&lock_);
        file_ = CreateFileW(
            path.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        const bool opened = file_ != INVALID_HANDLE_VALUE;
        ReleaseSRWLockExclusive(&lock_);

        if (!opened) {
            OutputDebugStringA("[CustomModel] could not open the log file\n");
        }
        return opened;
    } catch (...) {
        OutputDebugStringA("[CustomModel] logging initialization failed\n");
        return false;
    }
}

void Logger::Info(std::string_view message) noexcept {
    Write("CustomModel", "", message);
}

void Logger::Debug(std::string_view message) noexcept {
    Write("CustomModel:debug", "", message);
}

void Logger::Error(std::string_view message) noexcept {
    Write("CustomModel", "error: ", message);
}

void Logger::Write(
    std::string_view tag,
    std::string_view level,
    std::string_view message
) noexcept {
    try {
        SYSTEMTIME time{};
        GetLocalTime(&time);

        std::array<char, 64> prefix{};
        const int prefixLength = std::snprintf(
            prefix.data(),
            prefix.size(),
            "%04u-%02u-%02u %02u:%02u:%02u ",
            static_cast<unsigned>(time.wYear),
            static_cast<unsigned>(time.wMonth),
            static_cast<unsigned>(time.wDay),
            static_cast<unsigned>(time.wHour),
            static_cast<unsigned>(time.wMinute),
            static_cast<unsigned>(time.wSecond)
        );
        if (prefixLength <= 0) {
            return;
        }

        std::string line;
        line.reserve(
            static_cast<std::size_t>(prefixLength) + tag.size() + level.size() +
            message.size() + 5
        );
        line.append(prefix.data(), static_cast<std::size_t>(prefixLength));
        line.push_back('[');
        line.append(tag);
        line.append("] ");
        line.append(level);
        line.append(message);
        line.append("\r\n");

        AcquireSRWLockExclusive(&lock_);
        WriteUnlocked(line);
        ReleaseSRWLockExclusive(&lock_);

        OutputDebugStringA(line.c_str());
    } catch (...) {
        OutputDebugStringA("[CustomModel] logging failed\n");
    }
}

void Logger::WriteUnlocked(std::string_view text) noexcept {
    if (file_ == INVALID_HANDLE_VALUE || text.empty()) {
        return;
    }

    DWORD bytesWritten{};
    WriteFile(
        file_,
        text.data(),
        static_cast<DWORD>(text.size()),
        &bytesWritten,
        nullptr
    );
}

void Logger::ShutdownFromLoaderLock() noexcept {
    if (!TryAcquireSRWLockExclusive(&lock_)) {
        return;
    }

    if (file_ != INVALID_HANDLE_VALUE) {
        constexpr std::string_view message{"[CustomModel] shutting down\r\n"};
        WriteUnlocked(message);
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }

    ReleaseSRWLockExclusive(&lock_);
}

Logger& GetLogger() noexcept {
    return g_logger;
}

}
