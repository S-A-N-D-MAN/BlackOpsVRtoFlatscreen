// SPDX-License-Identifier: GPL-3.0-only
#include "t5/identity.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {
std::wstring quote(const std::wstring& value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') { result.append(slashes * 2 + 1, L'\\'); result.push_back(c); }
        else { result.append(slashes, L'\\'); result.push_back(c); }
        slashes = 0;
    }
    result.append(slashes * 2, L'\\'); result.push_back(L'\"'); return result;
}
std::vector<wchar_t> environment(const std::wstring& trace, bool enabled) {
    wchar_t* block = GetEnvironmentStringsW();
    if (!block) throw std::runtime_error("cannot copy environment");
    std::vector<std::wstring> strings;
    for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
        if (_wcsnicmp(p, L"BOVR_TRACE_FILE=", 16) && _wcsnicmp(p, L"BOVR_INSTRUMENT=", 16)) strings.emplace_back(p);
    }
    FreeEnvironmentStringsW(block);
    strings.push_back(L"BOVR_TRACE_FILE=" + trace);
    strings.push_back(enabled ? L"BOVR_INSTRUMENT=1" : L"BOVR_INSTRUMENT=0");
    std::sort(strings.begin(), strings.end(), [](const auto& a, const auto& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::vector<wchar_t> result;
    for (const auto& s : strings) { result.insert(result.end(), s.begin(), s.end()); result.push_back(0); }
    result.push_back(0); return result;
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        std::wstring game, trace;
        bool launch = false, enabled = true;
        std::vector<std::wstring> native;
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg = argv[i];
            if (arg == L"--game" && i + 1 < argc) game = argv[++i];
            else if (arg == L"--trace" && i + 1 < argc) trace = argv[++i];
            else if (arg == L"--launch") launch = true;
            else if (arg == L"--verify-only") launch = false;
            else if (arg == L"--stock-proxy") enabled = false;
            else if (arg == L"--") { while (++i < argc) native.emplace_back(argv[i]); break; }
            else throw std::runtime_error("unknown or incomplete launcher option");
        }
        if (game.empty()) {
            fwprintf(stderr, L"Usage: bovr-launch --game <isolated BlackOps.exe> [--verify-only] [--trace <absolute log>] [--stock-proxy] [--launch -- native arguments]\n");
            return 2;
        }
        game = std::filesystem::absolute(game).lexically_normal().wstring();
        const auto check = bovr::t5::validate_path(game);
        printf("identity accepted=%u sha256=%s reason=%s\n", check.accepted, check.sha256.c_str(), check.reason.c_str());
        if (!check.accepted) return 3;
        if (!launch) return 0;
        if (trace.empty() || !std::filesystem::path(trace).is_absolute())
            throw std::runtime_error("launch requires an absolute --trace path");
        const auto cwd = std::filesystem::path(game).parent_path();
        const auto proxy = cwd / L"d3d9.dll";
        if (!std::filesystem::is_regular_file(proxy)) throw std::runtime_error("stage diagnostic d3d9.dll beside the isolated game first");
        if (!std::filesystem::is_directory(std::filesystem::path(trace).parent_path()))
            throw std::runtime_error("trace parent directory must already exist");
        auto env = environment(trace, enabled);
        std::wstring command = quote(game);
        for (const auto& arg : native) command += L" " + quote(arg);
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(game.c_str(), command.data(), nullptr, nullptr, FALSE,
                            CREATE_UNICODE_ENVIRONMENT, env.data(), cwd.c_str(), &startup, &child)) {
            fprintf(stderr, "CreateProcess failed error=%lu\n", GetLastError()); return 4;
        }
        CloseHandle(child.hThread);
        printf("pid=%lu observation=%u started; launcher owns no termination timeout\n", child.dwProcessId, enabled);
        fflush(stdout);
        const DWORD wait = WaitForSingleObject(child.hProcess, INFINITE);
        DWORD exit_code = 5;
        if (wait == WAIT_OBJECT_0) GetExitCodeProcess(child.hProcess, &exit_code);
        CloseHandle(child.hProcess);
        printf("pid=%lu exit=0x%08lx\n", child.dwProcessId, exit_code);
        return static_cast<int>(exit_code);
    } catch (const std::exception& e) { fprintf(stderr, "%s\n", e.what()); return 2; }
}
