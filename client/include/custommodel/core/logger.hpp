#pragma once

#include <string_view>

#include <windows.h>

namespace custommodel::core {

class Logger final {
public:
    bool Open(HMODULE module) noexcept;
    void Info(std::string_view message) noexcept;
    void Debug(std::string_view message) noexcept;
    void Error(std::string_view message) noexcept;
    void ShutdownFromLoaderLock() noexcept;

private:
    void Write(
        std::string_view tag,
        std::string_view level,
        std::string_view message
    ) noexcept;
    void WriteUnlocked(std::string_view text) noexcept;

    SRWLOCK lock_{SRWLOCK_INIT};
    HANDLE file_{INVALID_HANDLE_VALUE};
};

Logger& GetLogger() noexcept;

}
