#include "gui.h"
#include "backup.h"
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <fstream>

#pragma comment(lib, "comctl32.lib")

#define ID_BTN_START     101
#define ID_BTN_STOP      102
#define ID_BTN_BACKUP    103  // 新增一键备份按钮ID
#define ID_EDT_SOURCE    104
#define ID_EDT_TARGET    105
#define ID_LOG_BOX       106
#define ID_TRAY_EXIT     201
#define ID_TRAY_SHOW     202

const wchar_t CLASS_NAME[] = L"BackupApp";
HINSTANCE g_hInstance;
HWND hMainWnd, hLogBox;

NOTIFYICONDATAW nid = {};

BackupManager backupMgr;  // 备份管理器实例

void AddTrayIcon(HWND hWnd) {
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"自动备份工具");

    Shell_NotifyIconW(NIM_ADD, &nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowContextMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"显示窗口");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");

    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

void Log(const std::wstring& msg) {
    SendMessageW(hLogBox, EM_SETSEL, -1, -1);
    SendMessageW(hLogBox, EM_REPLACESEL, 0, (LPARAM)msg.c_str());
    SendMessageW(hLogBox, EM_REPLACESEL, 0, (LPARAM)L"\r\n");
}

// 配置文件保存函数
void SaveConfig(const std::wstring& sourcePath, const std::wstring& targetPath) {
    std::wofstream ofs(L"config.ini");
    if (ofs) {
        ofs << sourcePath << L"\n" << targetPath << L"\n";
    }
}

// 配置文件读取函数
void LoadConfig(std::wstring& sourcePath, std::wstring& targetPath) {
    std::wifstream ifs(L"config.ini");
    if (ifs) {
        std::getline(ifs, sourcePath);
        std::getline(ifs, targetPath);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            CreateWindowW(L"static", L"源文件路径:", WS_CHILD | WS_VISIBLE,
                10, 10, 90, 20, hwnd, NULL, g_hInstance, NULL);

            CreateWindowW(L"edit", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
                110, 10, 340, 20, hwnd, (HMENU)ID_EDT_SOURCE, g_hInstance, NULL);

            CreateWindowW(L"static", L"目标目录:", WS_CHILD | WS_VISIBLE,
                10, 40, 90, 20, hwnd, NULL, g_hInstance, NULL);

            CreateWindowW(L"edit", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
                110, 40, 340, 20, hwnd, (HMENU)ID_EDT_TARGET, g_hInstance, NULL);

            CreateWindowW(L"button", L"开始监听", WS_CHILD | WS_VISIBLE,
                110, 70, 100, 30, hwnd, (HMENU)ID_BTN_START, g_hInstance, NULL);

            CreateWindowW(L"button", L"停止", WS_CHILD | WS_VISIBLE,
                230, 70, 100, 30, hwnd, (HMENU)ID_BTN_STOP, g_hInstance, NULL);

            CreateWindowW(L"button", L"立即备份", WS_CHILD | WS_VISIBLE,
                350, 70, 100, 30, hwnd, (HMENU)ID_BTN_BACKUP, g_hInstance, NULL);

            hLogBox = CreateWindowW(L"edit", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL,
                10, 110, 440, 150, hwnd, (HMENU)ID_LOG_BOX, g_hInstance, NULL);

            AddTrayIcon(hwnd);

            // 读取配置，填充输入框
            std::wstring src, tgt;
            LoadConfig(src, tgt);
            SetWindowTextW(GetDlgItem(hwnd, ID_EDT_SOURCE), src.c_str());
            SetWindowTextW(GetDlgItem(hwnd, ID_EDT_TARGET), tgt.c_str());

            break;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_BTN_START: {
                    wchar_t sourcePath[512] = { 0 };
                    GetWindowTextW(GetDlgItem(hwnd, ID_EDT_SOURCE), sourcePath, 512);
                    backupMgr.setWatchFile(sourcePath);

                    backupMgr.clearBackupTargets();

                    wchar_t targetPath[512] = { 0 };
                    GetWindowTextW(GetDlgItem(hwnd, ID_EDT_TARGET), targetPath, 512);
                    backupMgr.addBackupTarget(targetPath);

                    SaveConfig(sourcePath, targetPath);

                    if (backupMgr.startWatching()) {
                        Log(L"🟢 监听已开始...");
                    }
                    else {
                        Log(L"❌ 监听启动失败，可能已经在运行");
                    }
                    break;
                }
                case ID_BTN_STOP:
                    backupMgr.stopWatching();
                    Log(L"🛑 监听已停止。");
                    break;
                case ID_BTN_BACKUP: {
                    backupMgr.backupFile();
                    Log(L"💾 手动备份已执行");
                    // 保存当前路径，方便下次自动填写
                    wchar_t sourcePath[512] = { 0 };
                    wchar_t targetPath[512] = { 0 };
                    GetWindowTextW(GetDlgItem(hwnd, ID_EDT_SOURCE), sourcePath, 512);
                    GetWindowTextW(GetDlgItem(hwnd, ID_EDT_TARGET), targetPath, 512);
                    SaveConfig(sourcePath, targetPath);
                    break;
                }
                case ID_TRAY_EXIT:
                    PostQuitMessage(0);
                    break;
                case ID_TRAY_SHOW:
                    ShowWindow(hwnd, SW_SHOW);
                    break;
            }
            break;
        }
        case WM_USER + 1:
            if (lParam == WM_RBUTTONUP) {
                ShowContextMenu(hwnd);
            }
            break;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            RemoveTrayIcon();
            PostQuitMessage(0);
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int RunBackupApp(HINSTANCE hInstance, int nCmdShow) {
    g_hInstance = hInstance;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassW(&wc);

    hMainWnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"自动备份工具",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 480, 320,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    ShowWindow(hMainWnd, nCmdShow);
    UpdateWindow(hMainWnd);

    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
