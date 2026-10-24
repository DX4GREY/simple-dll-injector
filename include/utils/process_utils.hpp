#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

namespace utils {

struct ProcessEntryData {
    DWORD pid;
    std::wstring name;
};

// Formats a Win32 error code into a human-readable string
std::wstring GetSystemErrorMessage(DWORD errorCode);

// Determines the architecture of a process: 0 = Error, 32 = x86, 64 = x64
int GetProcessArchitecture(HANDLE hProcess);

// Returns the architecture of the current running injector binary: 32 or 64
int GetCurrentBinaryArchitecture();

// Fetches all running processes on the system (excluding idle/system PID <= 4)
std::vector<ProcessEntryData> FetchAllProcesses();

// Searches for a process ID by executable name (case-insensitive)
DWORD FindProcessIdByName(const std::wstring& targetName);

} // namespace utils
