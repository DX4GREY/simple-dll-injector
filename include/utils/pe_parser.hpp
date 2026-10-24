#pragma once

#include <string>

namespace utils {

// Returns PE architecture: 0 = Invalid/Error, 32 = x86, 64 = x64
int GetDllArchitecture(const std::wstring& dllPath);

} // namespace utils
