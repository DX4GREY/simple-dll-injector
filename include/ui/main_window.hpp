#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ui {

class MainWindow {
public:
    static int Run(HINSTANCE hInstance, int nCmdShow);

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

} // namespace ui
