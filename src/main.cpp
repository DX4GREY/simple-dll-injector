#include "ui/main_window.hpp"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    return ui::MainWindow::Run(hInstance, nCmdShow);
}
