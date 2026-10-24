#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

namespace core {

struct InjectionResult {
    bool success = false;
    bool isMismatch = false;
    std::wstring message;
};

// Injects the specified DLL into the target process using CreateRemoteThread and LoadLibraryW
InjectionResult PerformInjection(DWORD processId, const std::wstring& dllPath);

// Ejects (unloads) the specified DLL module from the target process using FreeLibrary
InjectionResult PerformEjection(DWORD processId, const std::wstring& dllPath);

} // namespace core
