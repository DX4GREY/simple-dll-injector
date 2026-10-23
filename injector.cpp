#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h> // <-- Tambahkan header ini untuk dialog file Browse
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")

// Aktifkan visual style modern (Windows Common Controls v6)
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

#define IDC_COMBO_DLL     101
#define IDC_BTN_BROWSE    102
#define IDC_COMBO_PROC    103
#define IDC_BTN_REFRESH   104
#define IDC_BTN_INJECT    105
#define IDC_STATUS_TEXT   106

HWND hComboDll, hComboProc, hStatusText;
std::vector<DWORD> processPids;

bool PerformInjection(DWORD processId, const std::wstring& dllPath) {
    if (!fs::exists(dllPath)) {
        MessageBoxW(nullptr, L"File DLL tidak ditemukan!", L"Error", MB_ICONERROR);
        return false;
    }

    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                  PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                  FALSE, processId);
    if (!hProcess) {
        MessageBoxW(nullptr, (L"Gagal membuka proses. Error: " + std::to_wstring(GetLastError())).c_str(), L"Error", MB_ICONERROR);
        return false;
    }

    size_t sizeInBytes = (dllPath.length() + 1) * sizeof(wchar_t);
    LPVOID pDllPath = VirtualAllocEx(hProcess, nullptr, sizeInBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pDllPath) {
        CloseHandle(hProcess);
        MessageBoxW(nullptr, L"Alokasi memori virtual gagal!", L"Error", MB_ICONERROR);
        return false;
    }

    if (!WriteProcessMemory(hProcess, pDllPath, dllPath.c_str(), sizeInBytes, nullptr)) {
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        MessageBoxW(nullptr, L"Gagal menulis memori target!", L"Error", MB_ICONERROR);
        return false;
    }

    LPTHREAD_START_ROUTINE pLoadLibraryW = (LPTHREAD_START_ROUTINE)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"
    );

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, pLoadLibraryW, pDllPath, 0, nullptr);
    if (!hThread) {
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        MessageBoxW(nullptr, L"Gagal mengeksekusi Remote Thread!", L"Error", MB_ICONERROR);
        return false;
    }

    WaitForSingleObject(hThread, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    return exitCode != 0;
}

void PopulateDllList(HWND hWnd) {
    SendMessageW(hComboDll, CB_RESETCONTENT, 0, 0);
    try {
        for (const auto& entry : fs::directory_iterator(fs::current_path())) {
            if (entry.is_regular_file() && entry.path().extension() == L".dll") {
                SendMessageW(hComboDll, CB_ADDSTRING, 0, (LPARAM)entry.path().filename().c_str());
            }
        }
    } catch (...) {}
    SendMessageW(hComboDll, CB_SETCURSEL, 0, 0);
}

void PopulateProcessList(HWND hWnd) {
    SendMessageW(hComboProc, CB_RESETCONTENT, 0, 0);
    processPids.clear();

    struct ProcEntry {
        DWORD pid;
        std::wstring name;
    };
    std::vector<ProcEntry> procs;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (pe.th32ProcessID > 4) {
                    procs.push_back({ pe.th32ProcessID, pe.szExeFile });
                }
            } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }

    std::sort(procs.begin(), procs.end(), [](const ProcEntry& a, const ProcEntry& b) {
        return a.name < b.name;
    });

    int gtaIndex = -1;
    for (size_t i = 0; i < procs.size(); ++i) {
        std::wstring display = procs[i].name + L" (PID: " + std::to_wstring(procs[i].pid) + L")";
        SendMessageW(hComboProc, CB_ADDSTRING, 0, (LPARAM)display.c_str());
        processPids.push_back(procs[i].pid);

        if (_wcsicmp(procs[i].name.c_str(), L"GTA5_Enhanced.exe") == 0 ||
            _wcsicmp(procs[i].name.c_str(), L"GTA5.exe") == 0) {
            gtaIndex = static_cast<int>(i);
        }
    }

    if (gtaIndex != -1) {
        SendMessageW(hComboProc, CB_SETCURSEL, gtaIndex, 0);
    } else {
        SendMessageW(hComboProc, CB_SETCURSEL, 0, 0);
    }
}

void BrowseDllFile(HWND hWnd) {
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
        int idx = (int)SendMessageW(hComboDll, CB_ADDSTRING, 0, (LPARAM)szFile);
        SendMessageW(hComboDll, CB_SETCURSEL, idx, 0);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HFONT hFont = nullptr;

    switch (msg) {
    case WM_CREATE: {
        hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        // Label DLL
        HWND lblDll = CreateWindowW(L"STATIC", L"Target DLL:", WS_CHILD | WS_VISIBLE,
                                    20, 15, 100, 18, hWnd, nullptr, nullptr, nullptr);

        hComboDll = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL,
                                  20, 35, 340, 200, hWnd, (HMENU)IDC_COMBO_DLL, nullptr, nullptr);

        HWND btnBrowse = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE,
                                      370, 34, 80, 26, hWnd, (HMENU)IDC_BTN_BROWSE, nullptr, nullptr);

        // Label Proses
        HWND lblProc = CreateWindowW(L"STATIC", L"Target Process:", WS_CHILD | WS_VISIBLE,
                                     20, 75, 120, 18, hWnd, nullptr, nullptr, nullptr);

        hComboProc = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                   20, 95, 340, 300, hWnd, (HMENU)IDC_COMBO_PROC, nullptr, nullptr);

        HWND btnRefresh = CreateWindowW(L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE,
                                       370, 94, 80, 26, hWnd, (HMENU)IDC_BTN_REFRESH, nullptr, nullptr);

        // Tombol Inject
        HWND btnInject = CreateWindowW(L"BUTTON", L"INJECT DLL", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                      20, 140, 430, 38, hWnd, (HMENU)IDC_BTN_INJECT, nullptr, nullptr);

        // Status
        hStatusText = CreateWindowW(L"STATIC", L"Status: Ready", WS_CHILD | WS_VISIBLE,
                                    20, 188, 430, 20, hWnd, (HMENU)IDC_STATUS_TEXT, nullptr, nullptr);

        // Terapkan font Segoe UI ke semua control
        HWND controls[] = { lblDll, hComboDll, btnBrowse, lblProc, hComboProc, btnRefresh, btnInject, hStatusText };
        for (HWND ctrl : controls) {
            SendMessageW(ctrl, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        PopulateDllList(hWnd);
        PopulateProcessList(hWnd);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_BTN_BROWSE) {
            BrowseDllFile(hWnd);
        } else if (id == IDC_BTN_REFRESH) {
            PopulateProcessList(hWnd);
            SetWindowTextW(hStatusText, L"Status: Process list refreshed");
        } else if (id == IDC_BTN_INJECT) {
            int procIdx = (int)SendMessageW(hComboProc, CB_GETCURSEL, 0, 0);
            if (procIdx < 0 || procIdx >= static_cast<int>(processPids.size())) {
                MessageBoxW(hWnd, L"Pilih target proses terlebih dahulu!", L"Peringatan", MB_ICONWARNING);
                break;
            }

            wchar_t dllBuffer[MAX_PATH];
            GetWindowTextW(hComboDll, dllBuffer, MAX_PATH);
            std::wstring dllPath = dllBuffer;

            if (dllPath.empty()) {
                MessageBoxW(hWnd, L"Pilih atau masukkan file DLL!", L"Peringatan", MB_ICONWARNING);
                break;
            }

            DWORD pid = processPids[procIdx];
            SetWindowTextW(hStatusText, L"Status: Injecting...");

            if (PerformInjection(pid, dllPath)) {
                SetWindowTextW(hStatusText, L"Status: Injeksi sukses!");
                MessageBoxW(hWnd, L"DLL berhasil diinjeksi ke target!", L"Sukses", MB_ICONINFORMATION);
            } else {
                SetWindowTextW(hStatusText, L"Status: Injeksi gagal");
            }
        }
        break;
    }
    case WM_DESTROY:
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

    const wchar_t CLASS_NAME[] = L"DllInjectorGuiWindow";

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    RegisterClassW(&wc);

    HWND hWnd = CreateWindowExW(
        0, CLASS_NAME, L"Native DLL Injector",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 485, 255,
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
