#include "utils/pe_parser.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace utils {

int GetDllArchitecture(const std::wstring& dllPath) {
    HANDLE hFile = CreateFileW(
        dllPath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) return 0;

    IMAGE_DOS_HEADER dosHeader;
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, &dosHeader, sizeof(dosHeader), &bytesRead, nullptr) ||
        dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
        CloseHandle(hFile);
        return 0;
    }

    SetFilePointer(hFile, dosHeader.e_lfanew, nullptr, FILE_BEGIN);
    IMAGE_NT_HEADERS ntHeaders;
    if (!ReadFile(hFile, &ntHeaders, sizeof(ntHeaders), &bytesRead, nullptr) ||
        ntHeaders.Signature != IMAGE_NT_SIGNATURE) {
        CloseHandle(hFile);
        return 0;
    }

    CloseHandle(hFile);

    if (ntHeaders.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64) return 64;
    if (ntHeaders.FileHeader.Machine == IMAGE_FILE_MACHINE_I386) return 32;
    return 0;
}

} // namespace utils
