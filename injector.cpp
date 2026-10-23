#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <atomic>
#include <thread>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

#define IDC_COMBO_DLL       101
#define IDC_BTN_BROWSE      102
#define IDC_SEARCH_PROC     103
#define IDC_COMBO_PROC      104
#define IDC_BTN_REFRESH     105
#define IDC_BTN_INJECT      106
#define IDC_BTN_EJECT       107
#define IDC_CHK_AUTOINJECT  108
#define IDC_STATUS_TEXT     109

struct ProcessEntryData {
    DWORD pid;
    std::wstring name;
};

HWND hMainWnd       = nullptr;
HWND hComboDll      = nullptr;
HWND hSearchProc    = nullptr;
HWND hComboProc     = nullptr;
HWND hChkAuto       = nullptr;
HWND hStatusText    = nullptr;

std::vector<ProcessEntryData> g_allProcesses;
std::vector<DWORD> g_displayedPids;

std::atomic<bool> g_autoInjectRunning(false);
std::thread g_autoInjectThread;

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

void SetStatus(const std::wstring& msg) {
    if (hStatusText) {
        SetWindowTextW(hStatusText, (L"Status: " + msg).c_str());
    }
}

// 0: Unknown/Error, 32: x86, 64: x64
int GetDllArchitecture(const std::wstring& dllPath) {
    HANDLE hFile = CreateFileW(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return 0;

    IMAGE_DOS_HEADER dosHeader;
    DWORD bytesRead = 0;
    if (!ReadFile(hFile, &dosHeader, sizeof(dosHeader), &bytesRead, nullptr) || dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
        CloseHandle(hFile);
        return 0;
    }

    SetFilePointer(hFile, dosHeader.e_lfanew, nullptr, FILE_BEGIN);
    IMAGE_NT_HEADERS ntHeaders;
    if (!ReadFile(hFile, &ntHeaders, sizeof(ntHeaders), &bytesRead, nullptr) || ntHeaders.Signature != IMAGE_NT_SIGNATURE) {
        CloseHandle(hFile);
        return 0;
    }

    CloseHandle(hFile);
    if (ntHeaders.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64) return 64;
    if (ntHeaders.FileHeader.Machine == IMAGE_FILE_MACHINE_I386) return 32;
    return 0;
}

// 0: Error, 32: x86, 64: x64
int GetProcessArchitecture(HANDLE hProcess) {
    BOOL isWow64 = FALSE;
    if (!IsWow64Process(hProcess, &isWow64)) return 0;

    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);

    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
        return isWow64 ? 32 : 64;
    }
    return 32;
}

bool PerformInjection(DWORD processId, const std::wstring& dllPath) {
    if (!fs::exists(dllPath)) {
        SetStatus(L"File DLL tidak ditemukan.");
        MessageBoxW(hMainWnd, L"File DLL tidak ditemukan!", L"Error", MB_ICONERROR);
        return false;
    }

    int dllArch = GetDllArchitecture(dllPath);
    if (dllArch == 0) {
        SetStatus(L"Format DLL tidak valid / header PE rusak.");
        MessageBoxW(hMainWnd, L"File bukan DLL PE yang valid!", L"Error", MB_ICONERROR);
        return false;
    }

    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                  PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                  FALSE, processId);
    if (!hProcess) {
        DWORD err = GetLastError();
        SetStatus(L"OpenProcess gagal: " + GetSystemErrorMessage(err));
        return false;
    }

    int procArch = GetProcessArchitecture(hProcess);
    if (procArch != 0 && dllArch != procArch) {
        CloseHandle(hProcess);
        std::wstring errStr = L"Arsitektur tidak cocok! DLL: " + std::to_wstring(dllArch) + 
                              L"-bit, Target: " + std::to_wstring(procArch) + L"-bit.";
        SetStatus(errStr);
        MessageBoxW(hMainWnd, errStr.c_str(), L"Architecture Mismatch", MB_ICONERROR);
        return false;
    }

    size_t sizeInBytes = (dllPath.length() + 1) * sizeof(wchar_t);
    LPVOID pDllPath = VirtualAllocEx(hProcess, nullptr, sizeInBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pDllPath) {
        DWORD err = GetLastError();
        CloseHandle(hProcess);
        SetStatus(L"VirtualAllocEx gagal: " + GetSystemErrorMessage(err));
        return false;
    }

    if (!WriteProcessMemory(hProcess, pDllPath, dllPath.c_str(), sizeInBytes, nullptr)) {
        DWORD err = GetLastError();
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        SetStatus(L"WriteProcessMemory gagal: " + GetSystemErrorMessage(err));
        return false;
    }

    LPTHREAD_START_ROUTINE pLoadLibraryW = (LPTHREAD_START_ROUTINE)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"
    );

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, pLoadLibraryW, pDllPath, 0, nullptr);
    if (!hThread) {
        DWORD err = GetLastError();
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        SetStatus(L"CreateRemoteThread gagal: " + GetSystemErrorMessage(err));
        return false;
    }

    SetStatus(L"Menunggu eksekusi thread...");
    WaitForSingleObject(hThread, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    if (exitCode == 0) {
        SetStatus(L"Injeksi gagal (LoadLibraryW return NULL).");
        return false;
    }

    SetStatus(L"Injeksi sukses!");
    return true;
}

bool PerformEjection(DWORD processId, const std::wstring& dllPath) {
    std::wstring dllFilename = fs::path(dllPath).filename().wstring();

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        SetStatus(L"Gagal mengambil modul snapshot.");
        return false;
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
        SetStatus(L"Modul DLL tidak aktif di proses target.");
        MessageBoxW(hMainWnd, L"Modul DLL tersebut belum terinjeksi atau tidak ditemukan di proses target!", L"Info", MB_ICONINFORMATION);
        return false;
    }

    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                  PROCESS_VM_OPERATION, FALSE, processId);
    if (!hProcess) {
        SetStatus(L"OpenProcess gagal: " + GetSystemErrorMessage(GetLastError()));
        return false;
    }

    LPTHREAD_START_ROUTINE pFreeLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "FreeLibrary"
    );

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, pFreeLibrary, (LPVOID)hTargetModule, 0, nullptr);
    if (!hThread) {
        CloseHandle(hProcess);
        SetStatus(L"Gagal membuat thread FreeLibrary.");
        return false;
    }

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    CloseHandle(hProcess);

    SetStatus(L"DLL berhasil di-eject!");
    MessageBoxW(hMainWnd, L"Modul DLL berhasil dilepaskan (unloaded) dari proses!", L"Sukses", MB_ICONINFORMATION);
    return true;
}

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

void PopulateDllList() {
    SendMessageW(hComboDll, CB_RESETCONTENT, 0, 0);
    try {
        for (const auto& entry : fs::directory_iterator(fs::current_path())) {
            if (entry.is_regular_file() && entry.path().extension() == L".dll") {
                SendMessageW(hComboDll, CB_ADDSTRING, 0, (LPARAM)entry.path().filename().c_str());
            }
        }
    } catch (...) {}
}

void FetchAllProcesses() {
    g_allProcesses.clear();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (pe.th32ProcessID > 4) {
                    g_allProcesses.push_back({ pe.th32ProcessID, pe.szExeFile });
                }
            } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }
    std::sort(g_allProcesses.begin(), g_allProcesses.end(), [](const ProcessEntryData& a, const ProcessEntryData& b) {
        return a.name < b.name;
    });
}

void UpdateProcessComboBox(const std::wstring& filter = L"") {
    SendMessageW(hComboProc, CB_RESETCONTENT, 0, 0);
    g_displayedPids.clear();

    std::wstring lowerFilter = filter;
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::towlower);

    for (const auto& p : g_allProcesses) {
        std::wstring lowerName = p.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);

        if (lowerFilter.empty() || lowerName.find(lowerFilter) != std::wstring::npos) {
            std::wstring display = p.name + L" (PID: " + std::to_wstring(p.pid) + L")";
            SendMessageW(hComboProc, CB_ADDSTRING, 0, (LPARAM)display.c_str());
            g_displayedPids.push_back(p.pid);
        }
    }

    if (!g_displayedPids.empty()) {
        SendMessageW(hComboProc, CB_SETCURSEL, 0, 0);
    }
}

void AutoInjectWorker() {
    while (g_autoInjectRunning) {
        wchar_t searchBuf[MAX_PATH] = { 0 };
        GetWindowTextW(hSearchProc, searchBuf, MAX_PATH);
        std::wstring targetName = searchBuf;

        if (!targetName.empty()) {
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot != INVALID_HANDLE_VALUE) {
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

                if (foundPid != 0) {
                    wchar_t dllBuf[MAX_PATH] = { 0 };
                    GetWindowTextW(hComboDll, dllBuf, MAX_PATH);
                    std::wstring dllPath = dllBuf;

                    if (!dllPath.empty()) {
                        SetStatus(L"Proses terdeteksi! Memulai auto-injeksi...");
                        Sleep(1500); // Jeda singkat menunggu inisialisasi modul target
                        if (PerformInjection(foundPid, dllPath)) {
                            // Matikan auto-inject setelah sekali berhasil
                            SendMessageW(hChkAuto, BM_SETCHECK, BST_UNCHECKED, 0);
                            g_autoInjectRunning = false;
                            break;
                        }
                    }
                }
            }
        }
        Sleep(1000);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HFONT hFont = nullptr;

    switch (msg) {
    case WM_CREATE: {
        hMainWnd = hWnd;
        DragAcceptFiles(hWnd, TRUE);

        hFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        // Target DLL
        HWND lblDll = CreateWindowW(L"STATIC", L"Target DLL (Bisa Drag-and-Drop file ke sini):", WS_CHILD | WS_VISIBLE,
                                    20, 15, 340, 18, hWnd, nullptr, nullptr, nullptr);
        hComboDll = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL,
                                  20, 35, 340, 200, hWnd, (HMENU)IDC_COMBO_DLL, nullptr, nullptr);
        HWND btnBrowse = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE,
                                       370, 34, 85, 26, hWnd, (HMENU)IDC_BTN_BROWSE, nullptr, nullptr);

        // Filter / Search Process
        HWND lblSearch = CreateWindowW(L"STATIC", L"Filter Proses:", WS_CHILD | WS_VISIBLE,
                                      20, 72, 100, 18, hWnd, nullptr, nullptr, nullptr);
        hSearchProc = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                    20, 92, 340, 24, hWnd, (HMENU)IDC_SEARCH_PROC, nullptr, nullptr);

        // Target Process List
        HWND lblProc = CreateWindowW(L"STATIC", L"Pilih Proses:", WS_CHILD | WS_VISIBLE,
                                     20, 125, 100, 18, hWnd, nullptr, nullptr, nullptr);
        hComboProc = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                   20, 145, 340, 250, hWnd, (HMENU)IDC_COMBO_PROC, nullptr, nullptr);
        HWND btnRefresh = CreateWindowW(L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE,
                                        370, 144, 85, 26, hWnd, (HMENU)IDC_BTN_REFRESH, nullptr, nullptr);

        // Checkbox Auto-inject
        hChkAuto = CreateWindowW(L"BUTTON", L"Auto-Inject saat proses terdeteksi (Gunakan nama di Filter Proses)",
                                 WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                 20, 182, 435, 20, hWnd, (HMENU)IDC_CHK_AUTOINJECT, nullptr, nullptr);

        // Tombol Inject & Eject
        HWND btnInject = CreateWindowW(L"BUTTON", L"INJECT DLL", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                       20, 215, 210, 38, hWnd, (HMENU)IDC_BTN_INJECT, nullptr, nullptr);
        HWND btnEject = CreateWindowW(L"BUTTON", L"EJECT / UNLOAD", WS_CHILD | WS_VISIBLE,
                                      245, 215, 210, 38, hWnd, (HMENU)IDC_BTN_EJECT, nullptr, nullptr);

        // Status text
        hStatusText = CreateWindowW(L"STATIC", L"Status: Ready", WS_CHILD | WS_VISIBLE,
                                    20, 265, 435, 22, hWnd, (HMENU)IDC_STATUS_TEXT, nullptr, nullptr);

        HWND allControls[] = {
            lblDll, hComboDll, btnBrowse, lblSearch, hSearchProc,
            lblProc, hComboProc, btnRefresh, hChkAuto, btnInject, btnEject, hStatusText
        };
        for (HWND ctrl : allControls) {
            SendMessageW(ctrl, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        PopulateDllList();
        FetchAllProcesses();

        // Muat saved config
        std::wstring lastDll, lastProc;
        LoadConfig(lastDll, lastProc);
        if (!lastDll.empty()) {
            SetWindowTextW(hComboDll, lastDll.c_str());
        }
        if (!lastProc.empty()) {
            SetWindowTextW(hSearchProc, lastProc.c_str());
            UpdateProcessComboBox(lastProc);
        } else {
            UpdateProcessComboBox(L"");
        }
        break;
    }

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        wchar_t droppedFile[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, droppedFile, MAX_PATH)) {
            if (fs::path(droppedFile).extension() == L".dll") {
                SetWindowTextW(hComboDll, droppedFile);
                SetStatus(L"File DLL di-drop: " + fs::path(droppedFile).filename().wstring());
            } else {
                MessageBoxW(hWnd, L"File yang di-drop harus berekstensi .dll!", L"Peringatan", MB_ICONWARNING);
            }
        }
        DragFinish(hDrop);
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == IDC_SEARCH_PROC && code == EN_CHANGE) {
            wchar_t query[MAX_PATH];
            GetWindowTextW(hSearchProc, query, MAX_PATH);
            UpdateProcessComboBox(query);
        }
        else if (id == IDC_BTN_REFRESH) {
            FetchAllProcesses();
            wchar_t query[MAX_PATH];
            GetWindowTextW(hSearchProc, query, MAX_PATH);
            UpdateProcessComboBox(query);
            SetStatus(L"Daftar proses diperbarui.");
        }
        else if (id == IDC_BTN_BROWSE) {
            wchar_t szFile[MAX_PATH] = { 0 };
            OPENFILENAMEW ofn = { 0 };
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hWnd;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
            ofn.lpstrFilter = L"Dynamic Link Library (*.dll)\0*.dll\0All Files (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

            if (GetOpenFileNameW(&ofn)) {
                SetWindowTextW(hComboDll, szFile);
            }
        }
        else if (id == IDC_CHK_AUTOINJECT) {
            bool checked = (SendMessageW(hChkAuto, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (checked) {
                wchar_t searchBuf[MAX_PATH] = { 0 };
                GetWindowTextW(hSearchProc, searchBuf, MAX_PATH);
                if (wcslen(searchBuf) == 0) {
                    MessageBoxW(hWnd, L"Ketik nama file proses (contoh: GTA5_Enhanced.exe) pada kolom Filter Proses terlebih dahulu!", L"Info", MB_ICONINFORMATION);
                    SendMessageW(hChkAuto, BM_SETCHECK, BST_UNCHECKED, 0);
                    break;
                }
                g_autoInjectRunning = true;
                if (g_autoInjectThread.joinable()) g_autoInjectThread.join();
                g_autoInjectThread = std::thread(AutoInjectWorker);
                SetStatus(L"Auto-Inject aktif, menunggu proses target...");
            } else {
                g_autoInjectRunning = false;
                SetStatus(L"Auto-Inject dimatikan.");
            }
        }
        else if (id == IDC_BTN_INJECT || id == IDC_BTN_EJECT) {
            int curSel = (int)SendMessageW(hComboProc, CB_GETCURSEL, 0, 0);
            if (curSel < 0 || curSel >= static_cast<int>(g_displayedPids.size())) {
                MessageBoxW(hWnd, L"Pilih proses target terlebih dahulu!", L"Peringatan", MB_ICONWARNING);
                break;
            }

            wchar_t dllBuffer[MAX_PATH] = { 0 };
            GetWindowTextW(hComboDll, dllBuffer, MAX_PATH);
            std::wstring dllPath = dllBuffer;

            if (dllPath.empty()) {
                MessageBoxW(hWnd, L"Pilih atau masukkan path DLL terlebih dahulu!", L"Peringatan", MB_ICONWARNING);
                break;
            }

            wchar_t searchBuf[MAX_PATH] = { 0 };
            GetWindowTextW(hSearchProc, searchBuf, MAX_PATH);
            SaveConfig(dllPath, searchBuf);

            DWORD pid = g_displayedPids[curSel];

            if (id == IDC_BTN_INJECT) {
                if (PerformInjection(pid, dllPath)) {
                    MessageBoxW(hWnd, L"DLL berhasil diinjeksi ke target!", L"Sukses", MB_ICONINFORMATION);
                }
            } else {
                PerformEjection(pid, dllPath);
            }
        }
        break;
    }

    case WM_DESTROY:
        g_autoInjectRunning = false;
        if (g_autoInjectThread.joinable()) {
            g_autoInjectThread.join();
        }
        if (hFont) DeleteObject(hFont);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    const wchar_t CLASS_NAME[] = L"AdvancedDllInjectorWindow";

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    RegisterClassW(&wc);

    HWND hWnd = CreateWindowExW(
        0, CLASS_NAME, L"Native DLL Injector (Modular Edition)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 490, 340,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hWnd) return 0;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
