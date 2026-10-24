#include "config/config.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fstream>

namespace config {

void SaveConfig(const std::wstring& dllPath, const std::wstring& procName) {
    std::wofstream file("config.ini");
    if (file.is_open()) {
        file << L"[Settings]\n";
        file << L"LastDLL=" << dllPath << L"\n";
        file << L"LastProcess=" << procName << L"\n";
    }
}

void LoadConfig(std::wstring& outDll, std::wstring& outProc) {
    wchar_t dllBuffer[MAX_PATH] = { 0 };
    wchar_t procBuffer[MAX_PATH] = { 0 };
    GetPrivateProfileStringW(L"Settings", L"LastDLL", L"", dllBuffer, MAX_PATH, L".\\config.ini");
    GetPrivateProfileStringW(L"Settings", L"LastProcess", L"", procBuffer, MAX_PATH, L".\\config.ini");
    outDll = dllBuffer;
    outProc = procBuffer;
}

} // namespace config
