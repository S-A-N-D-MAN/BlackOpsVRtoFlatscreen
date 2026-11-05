// SPDX-License-Identifier: GPL-3.0-only
#include "loader/trace.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace bovr {
namespace { HANDLE log_file = INVALID_HANDLE_VALUE; SRWLOCK log_lock = SRWLOCK_INIT; }
void initialize_trace(HMODULE module) noexcept {
    const DWORD previous = GetLastError();
    wchar_t path[32768]{};
    DWORD n = GetEnvironmentVariableW(L"BOVR_TRACE_FILE", path, 32768);
    if (!n || n >= 32768) {
        n = GetModuleFileNameW(module, path, 32768);
        if (n && n < 32768) {
            auto* slash = wcsrchr(path, L'\\');
            if (slash) wcscpy_s(slash + 1, 32768 - static_cast<std::size_t>(slash + 1 - path), L"bovr-bootstrap.log");
            else path[0] = 0;
        } else path[0] = 0;
    }
    if (path[0]) log_file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    SetLastError(previous);
}
void trace(const char* event, const char* format, ...) noexcept {
    const DWORD previous = GetLastError();
    char details[1536]{}, line[1800]{};
    va_list args; va_start(args, format);
    _vsnprintf_s(details, sizeof(details), _TRUNCATE, format, args); va_end(args);
    _snprintf_s(line, sizeof(line), _TRUNCATE, "tick=%llu tid=%lu event=%s %s\r\n",
        GetTickCount64(), GetCurrentThreadId(), event, details);
    AcquireSRWLockExclusive(&log_lock);
    if (log_file != INVALID_HANDLE_VALUE) {
        DWORD written{};
        WriteFile(log_file, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
        // Keep each line immediately readable without forcing a storage-device
        // flush while native render/input threads hold the shared log lock.
    }
    ReleaseSRWLockExclusive(&log_lock);
    SetLastError(previous);
}
}
