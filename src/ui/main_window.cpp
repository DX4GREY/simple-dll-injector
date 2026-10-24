#include "ui/main_window.hpp"
#include "core/injector.hpp"
#include "core/auto_inject.hpp"
#include "utils/process_utils.hpp"
#include "config/config.hpp"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

namespace ui {

namespace {

constexpr int IDC_COMBO_DLL       = 101;
constexpr int IDC_BTN_BROWSE      = 102;
constexpr int IDC_SEARCH_PROC     = 103;
constexpr int IDC_COMBO_PROC      = 104;
constexpr int IDC_BTN_REFRESH     = 105;
constexpr int IDC_BTN_INJECT      = 106;
constexpr int IDC_BTN_EJECT       = 107;
constexpr int IDC_CHK_AUTOINJECT  = 108;
constexpr int IDC_STATUS_TEXT     = 109;

HWND g_hMainWnd       = nullptr;
HWND g_hComboDll      = nullptr;
HWND g_hSearchProc    = nullptr;
HWND g_hComboProc     = nullptr;
HWND g_hChkAuto       = nullptr;
HWND g_hStatusText    = nullptr;

std::vector<utils::ProcessEntryData> g_allProcesses;
std::vector<DWORD> g_displayedPids;
core::AutoInjectManager g_autoInjector;

void SetStatus(const std::wstring& msg) {
    if (g_hStatusText) {
        SetWindowTextW(g_hStatusText, (L"Status: " + msg).c_str());
    }
}

void PopulateDllList() {
    SendMessageW(g_hComboDll, CB_RESETCONTENT, 0, 0);
    try {
        for (const auto& entry : fs::directory_iterator(fs::current_path())) {
            if (entry.is_regular_file() && entry.path().extension() == L".dll") {
                SendMessageW(g_hComboDll, CB_ADDSTRING, 0, (LPARAM)entry.path().filename().c_str());
            }
        }
    } catch (...) {}
}

void RefreshProcesses() {
    g_allProcesses = utils::FetchAllProcesses();
}

void UpdateProcessComboBox(const std::wstring& filter = L"") {
    SendMessageW(g_hComboProc, CB_RESETCONTENT, 0, 0);
    g_displayedPids.clear();

    std::wstring lowerFilter = filter;
    std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::towlower);

    for (const auto& p : g_allProcesses) {
        std::wstring lowerName = p.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);

        if (lowerFilter.empty() || lowerName.find(lowerFilter) != std::wstring::npos) {
            std::wstring display = p.name + L" (PID: " + std::to_wstring(p.pid) + L")";
            SendMessageW(g_hComboProc, CB_ADDSTRING, 0, (LPARAM)display.c_str());
            g_displayedPids.push_back(p.pid);
        }
    }

    if (!g_displayedPids.empty()) {
        SendMessageW(g_hComboProc, CB_SETCURSEL, 0, 0);
    }
}

} // anonymous namespace

int MainWindow::Run(HINSTANCE hInstance, int nCmdShow) {
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
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HFONT hFont = nullptr;

    switch (msg) {
    case WM_CREATE: {
        g_hMainWnd = hWnd;
        DragAcceptFiles(hWnd, TRUE);

        hFont = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        // Target DLL Controls
        HWND lblDll = CreateWindowW(L"STATIC", L"Target DLL (Drag-and-Drop supported):", WS_CHILD | WS_VISIBLE,
                                    20, 15, 340, 18, hWnd, nullptr, nullptr, nullptr);
        g_hComboDll = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL,
                                    20, 35, 340, 200, hWnd, (HMENU)IDC_COMBO_DLL, nullptr, nullptr);
        HWND btnBrowse = CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE,
                                       370, 34, 85, 26, hWnd, (HMENU)IDC_BTN_BROWSE, nullptr, nullptr);

        // Filter / Search Process Controls
        HWND lblSearch = CreateWindowW(L"STATIC", L"Filter Process:", WS_CHILD | WS_VISIBLE,
                                       20, 72, 100, 18, hWnd, nullptr, nullptr, nullptr);
        g_hSearchProc = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                      20, 92, 340, 24, hWnd, (HMENU)IDC_SEARCH_PROC, nullptr, nullptr);

        // Target Process List Controls
        HWND lblProc = CreateWindowW(L"STATIC", L"Select Process:", WS_CHILD | WS_VISIBLE,
                                     20, 125, 100, 18, hWnd, nullptr, nullptr, nullptr);
        g_hComboProc = CreateWindowW(L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                     20, 145, 340, 250, hWnd, (HMENU)IDC_COMBO_PROC, nullptr, nullptr);
        HWND btnRefresh = CreateWindowW(L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE,
                                         370, 144, 85, 26, hWnd, (HMENU)IDC_BTN_REFRESH, nullptr, nullptr);

        // Auto-Inject Checkbox
        g_hChkAuto = CreateWindowW(L"BUTTON", L"Auto-Inject upon process detection (Uses name from Filter)",
                                   WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                   20, 182, 435, 20, hWnd, (HMENU)IDC_CHK_AUTOINJECT, nullptr, nullptr);

        // Action Buttons: Inject & Eject
        HWND btnInject = CreateWindowW(L"BUTTON", L"INJECT DLL", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                       20, 215, 210, 38, hWnd, (HMENU)IDC_BTN_INJECT, nullptr, nullptr);
        HWND btnEject = CreateWindowW(L"BUTTON", L"EJECT / UNLOAD", WS_CHILD | WS_VISIBLE,
                                      245, 215, 210, 38, hWnd, (HMENU)IDC_BTN_EJECT, nullptr, nullptr);

        // Status bar
        g_hStatusText = CreateWindowW(L"STATIC", L"Status: Ready", WS_CHILD | WS_VISIBLE,
                                      20, 265, 435, 22, hWnd, (HMENU)IDC_STATUS_TEXT, nullptr, nullptr);

        HWND allControls[] = {
            lblDll, g_hComboDll, btnBrowse, lblSearch, g_hSearchProc,
            lblProc, g_hComboProc, btnRefresh, g_hChkAuto, btnInject, btnEject, g_hStatusText
        };
        for (HWND ctrl : allControls) {
            SendMessageW(ctrl, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        PopulateDllList();
        RefreshProcesses();

        // Load saved configuration
        std::wstring lastDll, lastProc;
        config::LoadConfig(lastDll, lastProc);
        if (!lastDll.empty()) {
            SetWindowTextW(g_hComboDll, lastDll.c_str());
        }
        if (!lastProc.empty()) {
            SetWindowTextW(g_hSearchProc, lastProc.c_str());
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
                SetWindowTextW(g_hComboDll, droppedFile);
                SetStatus(L"DLL file dropped: " + fs::path(droppedFile).filename().wstring());
            } else {
                MessageBoxW(hWnd, L"Dropped file must have a .dll extension!", L"Warning", MB_ICONWARNING);
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
            GetWindowTextW(g_hSearchProc, query, MAX_PATH);
            UpdateProcessComboBox(query);
        }
        else if (id == IDC_BTN_REFRESH) {
            RefreshProcesses();
            wchar_t query[MAX_PATH];
            GetWindowTextW(g_hSearchProc, query, MAX_PATH);
            UpdateProcessComboBox(query);
            SetStatus(L"Process list updated.");
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
                SetWindowTextW(g_hComboDll, szFile);
            }
        }
        else if (id == IDC_CHK_AUTOINJECT) {
            bool checked = (SendMessageW(g_hChkAuto, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (checked) {
                wchar_t searchBuf[MAX_PATH] = { 0 };
                GetWindowTextW(g_hSearchProc, searchBuf, MAX_PATH);
                if (wcslen(searchBuf) == 0) {
                    MessageBoxW(hWnd, L"Please enter the process executable name (e.g., GTA5_Enhanced.exe) in the Process Filter field first!", L"Information", MB_ICONINFORMATION);
                    SendMessageW(g_hChkAuto, BM_SETCHECK, BST_UNCHECKED, 0);
                    break;
                }

                wchar_t dllBuf[MAX_PATH] = { 0 };
                GetWindowTextW(g_hComboDll, dllBuf, MAX_PATH);
                std::wstring dllPath = dllBuf;

                g_autoInjector.Start(
                    searchBuf,
                    dllPath,
                    [](const std::wstring& statusMsg) {
                        SetStatus(statusMsg);
                    },
                    [hWnd](bool success) {
                        if (success) {
                            PostMessageW(g_hChkAuto, BM_SETCHECK, BST_UNCHECKED, 0);
                            MessageBoxW(hWnd, L"Auto-Inject completed successfully!", L"Success", MB_ICONINFORMATION);
                        }
                    }
                );
            } else {
                g_autoInjector.Stop();
                SetStatus(L"Auto-Inject disabled.");
            }
        }
        else if (id == IDC_BTN_INJECT || id == IDC_BTN_EJECT) {
            int curSel = static_cast<int>(SendMessageW(g_hComboProc, CB_GETCURSEL, 0, 0));
            if (curSel < 0 || curSel >= static_cast<int>(g_displayedPids.size())) {
                MessageBoxW(hWnd, L"Please select a target process first!", L"Warning", MB_ICONWARNING);
                break;
            }

            wchar_t dllBuffer[MAX_PATH] = { 0 };
            GetWindowTextW(g_hComboDll, dllBuffer, MAX_PATH);
            std::wstring dllPath = dllBuffer;

            if (dllPath.empty()) {
                MessageBoxW(hWnd, L"Please select or specify a DLL path first!", L"Warning", MB_ICONWARNING);
                break;
            }

            wchar_t searchBuf[MAX_PATH] = { 0 };
            GetWindowTextW(g_hSearchProc, searchBuf, MAX_PATH);
            config::SaveConfig(dllPath, searchBuf);

            DWORD pid = g_displayedPids[curSel];

            if (id == IDC_BTN_INJECT) {
                core::InjectionResult res = core::PerformInjection(pid, dllPath);
                SetStatus(res.message);
                if (res.success) {
                    MessageBoxW(hWnd, res.message.c_str(), L"Success", MB_ICONINFORMATION);
                } else {
                    MessageBoxW(hWnd, res.message.c_str(), res.isMismatch ? L"Architecture Mismatch" : L"Error", MB_ICONERROR);
                }
            } else {
                core::InjectionResult res = core::PerformEjection(pid, dllPath);
                SetStatus(res.message);
                if (res.success) {
                    MessageBoxW(hWnd, res.message.c_str(), L"Success", MB_ICONINFORMATION);
                } else {
                    MessageBoxW(hWnd, res.message.c_str(), L"Information", MB_ICONINFORMATION);
                }
            }
        }
        break;
    }

    case WM_DESTROY:
        g_autoInjector.Stop();
        if (hFont) DeleteObject(hFont);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

} // namespace ui
