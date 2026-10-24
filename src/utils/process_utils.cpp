#include "utils/process_utils.hpp"

#include <tlhelp32.h>
#include <algorithm>

namespace utils {

std::wstring GetSystemErrorMessage(DWORD errorCode) {
    if (errorCode == 0) return L"Success";
    LPWSTR messageBuffer = nullptr;
    size_t size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&messageBuffer, 0, nullptr
    );
    std::wstring message(messageBuffer, size);
    LocalFree(messageBuffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    return message;
}

int GetProcessArchitecture(HANDLE hProcess) {
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);

    // If running on a 32-bit OS, everything is 32-bit
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) {
        return 32;
    }

    // On 64-bit OS: check if process is WOW64 (32-bit running on 64-bit)
    BOOL isWow64 = FALSE;
    if (!IsWow64Process(hProcess, &isWow64)) {
        return 0;
    }

    return isWow64 ? 32 : 64;
}

int GetCurrentBinaryArchitecture() {
#if defined(_WIN64)
    return 64;
#else
    return 32;
#endif
}

std::vector<ProcessEntryData> FetchAllProcesses() {
    std::vector<ProcessEntryData> processList;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (pe.th32ProcessID > 4) {
                    processList.push_back({ pe.th32ProcessID, pe.szExeFile });
                }
            } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }
    std::sort(processList.begin(), processList.end(), [](const ProcessEntryData& a, const ProcessEntryData& b) {
        return a.name < b.name;
    });
    return processList;
}

DWORD FindProcessIdByName(const std::wstring& targetName) {
    if (targetName.empty()) return 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    DWORD foundPid = 0;

    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, targetName.c_str()) == 0) {
                foundPid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);
    return foundPid;
}

} // namespace utils
