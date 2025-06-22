//Document
#include "gui.h"
#include "backup.h"
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <fstream>
#include <vector>
#include <shlwapi.h>
#include <ctime>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

#define ID_BTN_START     101
#define ID_BTN_STOP      102
#define ID_BTN_BACKUP    103
#define ID_EDT_SOURCE    104
#define ID_EDT_TARGET    105
#define ID_LOG_BOX       106
#define ID_TRAY_EXIT     201
#define ID_TRAY_SHOW     202
#define ID_CHK_AUTORUN   107

const wchar_t CLASS_NAME[] = L"BackupApp";
HINSTANCE g_hInstance;
HWND hMainWnd, hLogBox;

NOTIFYICONDATAW nid = {};

BackupManager backupMgr;

std::wstring GetCurrentTimeStr() {
    std::time_t t = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &t);
    wchar_t buf[64];
    wcsftime(buf, 64, L"[%Y-%m-%d %H:%M:%S] ", &local);
    return buf;
}

std::wstring GetExeDirectory() {
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    PathRemoveFileSpecW(buffer);
    return buffer;
}

void AddTrayIcon(HWND hWnd) {
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1;
    nid.hIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(101));
    wcscpy_s(nid.szTip, L"Document Automatic Backup V1.2.0");

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
    // 时间戳
    std::time_t t = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &t);
    wchar_t timebuf[64];
    wcsftime(timebuf, 64, L"[%Y-%m-%d %H:%M:%S] ", &local);
    std::wstring fullMsg = timebuf + msg + L"\r\n";

    if (hLogBox && IsWindow(hLogBox)) {
        SendMessageW(hLogBox, WM_SETREDRAW, FALSE, 0);

        int textLength = (int)SendMessageW(hLogBox, WM_GETTEXTLENGTH, 0, 0);
        SendMessageW(hLogBox, EM_SETSEL, textLength, textLength);
        SendMessageW(hLogBox, EM_REPLACESEL, FALSE, (LPARAM)fullMsg.c_str());

        // 先把插入点移到末尾
        int newLength = (int)SendMessageW(hLogBox, WM_GETTEXTLENGTH, 0, 0);
        SendMessageW(hLogBox, EM_SETSEL, newLength, newLength);

        // 先滚动光标
        SendMessageW(hLogBox, EM_SCROLLCARET, 0, 0);
        // 再强制往下滚动1行
        SendMessageW(hLogBox, EM_LINESCROLL, 0, 1);

        SendMessageW(hLogBox, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(hLogBox, NULL, TRUE);
        UpdateWindow(hLogBox);
    }


    // ✅ 写入日志文件（支持中文路径）
    std::wstring path = GetExeDirectory() + L"\\backup.log";

    HANDLE hFile = CreateFileW(
        path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        // 转为 UTF-8 编码输出
        int len = WideCharToMultiByte(CP_UTF8, 0, fullMsg.c_str(), -1, NULL, 0, NULL, NULL);
        if (len > 1) {
            std::string utf8(len - 1, '\0');  // 不包括 null 结尾
            WideCharToMultiByte(CP_UTF8, 0, fullMsg.c_str(), -1, &utf8[0], len, NULL, NULL);
            WriteFile(hFile, utf8.c_str(), (DWORD)utf8.size(), &written, NULL);
        }
        CloseHandle(hFile);
    } else {
        // 回显错误
        if (hLogBox && IsWindow(hLogBox)) {
            SendMessageW(hLogBox, EM_REPLACESEL, 0, (LPARAM)L"❌ 写日志失败：路径无效或被占用\r\n");
        }
    }
}


void SaveConfig(const std::wstring& sourcePath, const std::wstring& targetsMultiLine) {
    std::wstring content = sourcePath + L"\n" + targetsMultiLine + L"\n";
    std::wstring path = GetExeDirectory() + L"\\config.ini";

    HANDLE hFile = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        int len = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, NULL, 0, NULL, NULL);
        if (len > 1) {
            std::string utf8(len - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, &utf8[0], len, NULL, NULL);
            WriteFile(hFile, utf8.c_str(), (DWORD)utf8.size(), &written, NULL);
        }
        CloseHandle(hFile);
        Log(L"✅ 配置保存成功: " + path);
    } else {
        Log(L"❌ 无法保存配置: " + path);
    }
}

void TrimTrailingNewlines(std::wstring& str) {
    while (!str.empty() && (str.back() == L'\n' || str.back() == L'\r')) {
        str.pop_back();
    }
}

void LoadConfig(std::wstring& sourcePath, std::wstring& targetsMultiLine) {
    std::wstring path = GetExeDirectory() + L"\\config.ini";
    HANDLE hFile = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        sourcePath.clear();
        targetsMultiLine.clear();
        return;
    }

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize == 0) {
        CloseHandle(hFile);
        return;
    }

    std::string utf8Data(fileSize, '\0');
    DWORD bytesRead;
    ReadFile(hFile, &utf8Data[0], fileSize, &bytesRead, NULL);
    CloseHandle(hFile);

    // 转为 UTF-16（wstring）
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Data.c_str(), -1, NULL, 0);
    if (wideLen <= 0) return;
    std::wstring wContent(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Data.c_str(), -1, &wContent[0], wideLen);

    // 去掉结尾的 \0
    if (!wContent.empty() && wContent.back() == L'\0') {
        wContent.pop_back();
    }

    // 分离 sourcePath 和 targetMultiLine
    size_t pos = wContent.find(L'\n');
    if (pos == std::wstring::npos) {
        sourcePath = wContent;
        targetsMultiLine.clear();
    } else {
        sourcePath = wContent.substr(0, pos);
        targetsMultiLine = wContent.substr(pos + 1);
    }

    // 去掉尾部多余的 \r\n
    TrimTrailingNewlines(sourcePath);
    TrimTrailingNewlines(targetsMultiLine);
}

std::vector<std::wstring> SplitLines(const std::wstring& str) {
    std::vector<std::wstring> result;
    size_t start = 0;
    while (true) {
        size_t pos = str.find(L'\n', start);
        std::wstring line;
        if (pos == std::wstring::npos) {
            line = str.substr(start);
        } else {
            line = str.substr(start, pos - start);
        }

        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }

        if (!line.empty()) {
            result.push_back(line);
        }

        if (pos == std::wstring::npos) {
            break;
        }
        start = pos + 1;
    }
    return result;
}


bool CheckSingleInstance(HINSTANCE hInstance) {
    // 创建命名互斥体
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Global\\BackupAppMutex");
    if (hMutex == NULL) {
        MessageBoxW(NULL, L"创建互斥体失败，程序无法启动。", L"错误", MB_ICONERROR);
        return false;  // 失败退出
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // 说明已有实例运行
        int ret = MessageBoxW(NULL, 
            L"程序已经在运行，是否要启动一个新的实例？",
            L"程序已打开",
            MB_ICONQUESTION | MB_YESNO);

        if (ret == IDNO) {
            CloseHandle(hMutex);
            return false;  // 用户选择不启动新实例，退出程序
        }
        // 用户选择是，继续运行（注意这里没有关闭互斥体句柄，多个实例共用不同句柄）
    }

    // 记得不要关闭这个互斥体句柄，程序运行期间一直持有它
    // 结束时由系统回收
    return true;
}

// 设置开机自启
bool SetAutoRun(bool enable) {
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathQuoteSpacesW(exePath);

    HKEY hKey;
    LONG ret = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_WRITE, &hKey);
    if (ret != ERROR_SUCCESS) return false;

    if (enable) {
        ret = RegSetValueExW(hKey, L"MyBackupApp", 0, REG_SZ,
            (const BYTE*)exePath, (wcslen(exePath) + 1) * sizeof(wchar_t));
    } else {
        ret = RegDeleteValueW(hKey, L"MyBackupApp");
        if (ret == ERROR_FILE_NOT_FOUND) ret = ERROR_SUCCESS;
    }
    RegCloseKey(hKey);
    return ret == ERROR_SUCCESS;
}

// 检查是否已启用开机自启
bool IsAutoRunEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;

    wchar_t value[MAX_PATH] = {0};
    DWORD size = sizeof(value);
    LONG ret = RegQueryValueExW(hKey, L"MyBackupApp", NULL, NULL, (LPBYTE)value, &size);
    RegCloseKey(hKey);
    return (ret == ERROR_SUCCESS);
}

//1.1.4 日志窗口子类化 接收滚轮消息

WNDPROC g_OldLogProc = NULL;

LRESULT CALLBACK LogEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_VSCROLL:
        case WM_MOUSEWHEEL:
            {
                LRESULT ret = CallWindowProcW(g_OldLogProc, hwnd, msg, wParam, lParam);
                InvalidateRect(hwnd, NULL, TRUE);
                UpdateWindow(hwnd);
                return ret;
            }
        default:
            return CallWindowProcW(g_OldLogProc, hwnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    
    switch (msg) {

        case WM_ERASEBKGND: { //UPDV1.1.4
            // 使用默认背景色手动填充窗口背景
            HDC hdc;
            RECT rect;
            hdc = (HDC)wParam;
            GetClientRect(hwnd, &rect);
            FillRect(hdc, &rect, (HBRUSH)(COLOR_WINDOW+1));
            return 1;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetBkMode(hdcStatic, TRANSPARENT);
            return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);  // 返回主窗口背景色刷子
        }

        case WM_CREATE: {
            
            static HFONT hFont = CreateFontW(
                -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                FF_DONTCARE, L"Segoe UI"
            );

            // 封装字体应用函数
            auto ApplyUIFont = [&](HWND hCtrl) {
                SendMessageW(hCtrl, WM_SETFONT, (WPARAM)hFont, TRUE);
            };

            HWND hCopyright = CreateWindowW(L"static", L"Created by Xiaochuan © 2025",
                WS_CHILD | WS_VISIBLE | SS_RIGHT,
                250, 300, 200, 20, hwnd, NULL, g_hInstance, NULL);
            ApplyUIFont(hCopyright);

            HWND hLabel1 = CreateWindowW(L"static", L"源文件路径:", WS_CHILD | WS_VISIBLE,
                10, 10, 90, 20, hwnd, NULL, g_hInstance, NULL);
            ApplyUIFont(hLabel1);

            HWND hSourceEdit = CreateWindowW(L"edit", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                110, 10, 340, 25, hwnd, (HMENU)ID_EDT_SOURCE, g_hInstance, NULL);
            ApplyUIFont(hSourceEdit);

            HWND hLabel2 = CreateWindowW(L"static", L"目标目录列表（多行，每行一个路径）:", WS_CHILD | WS_VISIBLE,
                10, 40, 280, 20, hwnd, NULL, g_hInstance, NULL);
            ApplyUIFont(hLabel2);

            HWND hTargetEdit = CreateWindowW(L"edit", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | WS_VSCROLL | ES_AUTOVSCROLL,
                10, 65, 440, 60, hwnd, (HMENU)ID_EDT_TARGET, g_hInstance, NULL);
            ApplyUIFont(hTargetEdit);

            HWND hBtnStart = CreateWindowW(L"button", L"开始监听", WS_CHILD | WS_VISIBLE,
                110, 130, 100, 30, hwnd, (HMENU)ID_BTN_START, g_hInstance, NULL);
            ApplyUIFont(hBtnStart);

            HWND hBtnStop = CreateWindowW(L"button", L"停止", WS_CHILD | WS_VISIBLE,
                230, 130, 100, 30, hwnd, (HMENU)ID_BTN_STOP, g_hInstance, NULL);
            ApplyUIFont(hBtnStop);

            HWND hBtnBackup = CreateWindowW(L"button", L"立即备份", WS_CHILD | WS_VISIBLE,
                350, 130, 100, 30, hwnd, (HMENU)ID_BTN_BACKUP, g_hInstance, NULL);
            ApplyUIFont(hBtnBackup);

            HWND hChkAutoRun = CreateWindowW(L"button", L"开机自启", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                10, 135, 90, 25, hwnd, (HMENU)ID_CHK_AUTORUN, g_hInstance, NULL);
            ApplyUIFont(hChkAutoRun);

            hLogBox = CreateWindowW(L"edit", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | WS_VSCROLL | ES_AUTOVSCROLL,
                10, 170, 440, 120, hwnd, (HMENU)ID_LOG_BOX, g_hInstance, NULL);
            ApplyUIFont(hLogBox);
            SendMessageW(hLogBox, EM_SETREADONLY, TRUE, 0);

            g_OldLogProc = (WNDPROC)SetWindowLongPtrW(hLogBox, GWLP_WNDPROC, (LONG_PTR)LogEditProc);

            AddTrayIcon(hwnd);

            // 加载配置
            std::wstring src, targetsMultiLine;
            LoadConfig(src, targetsMultiLine);
            SetWindowTextW(hSourceEdit, src.c_str());
            SetWindowTextW(hTargetEdit, targetsMultiLine.c_str());

            // 设置复选框状态
            SendMessageW(hChkAutoRun, BM_SETCHECK,
                IsAutoRunEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

            // 开机自启时自动开始监听
            if (IsAutoRunEnabled() && !src.empty()) {
                if (!PathFileExistsW(src.c_str()) && !PathIsDirectoryW(src.c_str())) {
                    Log(L"❌ 开机自启：源路径无效，监听未启动");
                } else {
                    auto targets = SplitLines(targetsMultiLine);

                    backupMgr.setWatchFile(src);
                    backupMgr.clearBackupTargets();
                    for (const auto& t : targets) {
                        backupMgr.addBackupTarget(t);
                    }

                    if (backupMgr.startWatching()) {
                        Log(L"🟢 开机自启：监听已自动开始...");
                    } else {
                        Log(L"❌ 开机自启：监听启动失败");
                    }
                }
            }

            break;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case ID_BTN_START: {
                    wchar_t sourcePath[512] = {0};
                    GetWindowTextW(GetDlgItem(hwnd, ID_EDT_SOURCE), sourcePath, 512);

                    // 先检测路径是否存在
                    if (!PathFileExistsW(sourcePath)) {
                        Log(L"❌ 源文件路径不存在，无法开始监听！");
                        MessageBoxW(hwnd, L"源文件路径不存在，请检查路径是否正确。", L"错误", MB_ICONERROR);
                        break;
                    }

                    wchar_t targetsBuffer[2048] = {0};
                    GetWindowTextW(GetDlgItem(hwnd, ID_EDT_TARGET), targetsBuffer, 2048);

                    std::wstring targetsStr = targetsBuffer;
                    auto targets = SplitLines(targetsStr);

                    backupMgr.setWatchFile(sourcePath);
                    backupMgr.clearBackupTargets();
                    for (const auto& t : targets) {
                        backupMgr.addBackupTarget(t);
                    }

                    SaveConfig(sourcePath, targetsStr);

                    if (backupMgr.startWatching()) {
                        Log(L"🟢 监听已开始...");
                    } else {
                        Log(L"❌ 监听启动失败，可能已经在运行");
                    }
                    break;
                }
                case ID_BTN_STOP:
                    backupMgr.stopWatching();
                    Log(L"🛑 监听已停止。");
                    break;
                case ID_BTN_BACKUP: {
                    wchar_t sourcePath[512] = { 0 };
                    GetWindowTextW(GetDlgItem(hwnd, ID_EDT_SOURCE), sourcePath, 512);

                    if (!PathFileExistsW(sourcePath) && !PathIsDirectoryW(sourcePath)) {
                        Log(L"❌ 手动备份失败：源路径无效");
                        break;  // 不执行备份
                    }

                    wchar_t targetsBuffer[2048] = { 0 };
                    GetWindowTextW(GetDlgItem(hwnd, ID_EDT_TARGET), targetsBuffer, 2048);

                    std::wstring targetsStr = targetsBuffer;
                    auto targets = SplitLines(targetsStr);

                    backupMgr.setWatchFile(sourcePath);
                    backupMgr.clearBackupTargets();
                    for (const auto& t : targets) {
                        backupMgr.addBackupTarget(t);
                    }

                    backupMgr.backupFile();
                    Log(L"💾 手动备份已执行");
                    SaveConfig(sourcePath, targetsStr);
                    break;
                }
                case ID_CHK_AUTORUN: {
                    // 这里改成判断消息类型，确保只处理点击事件
                    if (HIWORD(wParam) == BN_CLICKED) {
                        BOOL checked = (SendMessageW(GetDlgItem(hwnd, ID_CHK_AUTORUN), BM_GETCHECK, 0, 0) == BST_CHECKED);
                        if (SetAutoRun(checked)) {
                            Log(checked ? L"✅ 开机自启已启用" : L"❎ 开机自启已禁用");
                        } else {
                            Log(L"❌ 设置开机自启失败");
                        }
                    }
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
    if (!CheckSingleInstance(hInstance)) return 0;

    g_hInstance = hInstance;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassW(&wc);

    hMainWnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"DAB V1.2.0",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 470, 360,
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
