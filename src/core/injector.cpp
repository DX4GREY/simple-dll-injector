#include "core/injector.hpp"
#include "utils/pe_parser.hpp"
#include "utils/process_utils.hpp"

#include <tlhelp32.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace core {

InjectionResult PerformInjection(DWORD processId, const std::wstring& dllPath) {
    InjectionResult result;

    if (!fs::exists(dllPath)) {
        result.message = L"DLL file not found.";
        return result;
    }

    int dllArch = utils::GetDllArchitecture(dllPath);
    if (dllArch == 0) {
        result.message = L"Invalid DLL format / corrupted PE header.";
        return result;
    }

    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, processId
    );
    if (!hProcess) {
        DWORD err = GetLastError();
        result.message = L"OpenProcess failed: " + utils::GetSystemErrorMessage(err);
        return result;
    }

    int procArch = utils::GetProcessArchitecture(hProcess);
    if (procArch != 0 && dllArch != procArch) {
        CloseHandle(hProcess);
        result.isMismatch = true;
        result.message = L"Architecture mismatch! DLL: " + std::to_wstring(dllArch) +
                         L"-bit, Target: " + std::to_wstring(procArch) + L"-bit.";
        return result;
    }

    size_t sizeInBytes = (dllPath.length() + 1) * sizeof(wchar_t);
    LPVOID pDllPath = VirtualAllocEx(hProcess, nullptr, sizeInBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pDllPath) {
        DWORD err = GetLastError();
        CloseHandle(hProcess);
        result.message = L"VirtualAllocEx failed: " + utils::GetSystemErrorMessage(err);
        return result;
    }

    if (!WriteProcessMemory(hProcess, pDllPath, dllPath.c_str(), sizeInBytes, nullptr)) {
        DWORD err = GetLastError();
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        result.message = L"WriteProcessMemory failed: " + utils::GetSystemErrorMessage(err);
        return result;
    }

    LPTHREAD_START_ROUTINE pLoadLibraryW = (LPTHREAD_START_ROUTINE)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"
    );

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, pLoadLibraryW, pDllPath, 0, nullptr);
    if (!hThread) {
        DWORD err = GetLastError();
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        result.message = L"CreateRemoteThread failed: " + utils::GetSystemErrorMessage(err);
        return result;
    }

    WaitForSingleObject(hThread, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    if (exitCode == 0) {
        result.message = L"Injection failed (LoadLibraryW returned NULL).";
        return result;
    }

    result.success = true;
    result.message = L"DLL successfully injected into target process!";
    return result;
}

InjectionResult PerformEjection(DWORD processId, const std::wstring& dllPath) {
    InjectionResult result;
    std::wstring dllFilename = fs::path(dllPath).filename().wstring();

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        result.message = L"Failed to create module snapshot.";
        return result;
    }

    MODULEENTRY32W me;
    me.dwSize = sizeof(me);
    HMODULE hTargetModule = nullptr;

    if (Module32FirstW(hSnapshot, &me)) {
        do {
            if (_wcsicmp(me.szModule, dllFilename.c_str()) == 0) {
                hTargetModule = me.hModule;
                break;
            }
        } while (Module32NextW(hSnapshot, &me));
    }
    CloseHandle(hSnapshot);

    if (!hTargetModule) {
        result.message = L"The specified DLL module is not loaded in the target process!";
        return result;
    }

    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION,
        FALSE, processId
    );
    if (!hProcess) {
        result.message = L"OpenProcess failed: " + utils::GetSystemErrorMessage(GetLastError());
        return result;
    }

    LPTHREAD_START_ROUTINE pFreeLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "FreeLibrary"
    );

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, pFreeLibrary, (LPVOID)hTargetModule, 0, nullptr);
    if (!hThread) {
        CloseHandle(hProcess);
        result.message = L"Failed to create FreeLibrary thread.";
        return result;
    }

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    CloseHandle(hProcess);

    result.success = true;
    result.message = L"DLL module has been successfully unloaded from the process!";
    return result;
}

} // namespace core
