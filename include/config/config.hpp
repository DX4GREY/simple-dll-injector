#pragma once

#include <string>

namespace config {

// Persists the last used DLL path and target process filter to config.ini
void SaveConfig(const std::wstring& dllPath, const std::wstring& procName);

// Retrieves the last used DLL path and target process filter from config.ini
void LoadConfig(std::wstring& outDll, std::wstring& outProc);

} // namespace config
