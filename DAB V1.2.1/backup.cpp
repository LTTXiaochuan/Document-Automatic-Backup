#include "backup.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <windows.h>
#include <algorithm>
#include <codecvt>
#include <thread>
#include <atomic>
#include <map>

BackupManager::BackupManager() : watching(false), hDir(INVALID_HANDLE_VALUE), pollingMode(false), pollingInterval(2000) {}

BackupManager::~BackupManager() {
    stopWatching();
}

void BackupManager::setWatchFile(const std::wstring& fullPath) {
    watchFilePath = fullPath;
    watchDir = std::filesystem::path(fullPath).parent_path().wstring();
    watchFileName = std::filesystem::path(fullPath).filename().wstring();
}

void BackupManager::addBackupTarget(const std::wstring& targetDir) {
    backupTargets.push_back(targetDir);
}

void BackupManager::clearBackupTargets() {
    backupTargets.clear();
}

void BackupManager::setPollingMode(bool enabled) {
    pollingMode = enabled;
}

void BackupManager::setPollingInterval(int ms) {
    pollingInterval = ms;
}

bool BackupManager::startWatching() {
    if (watching.load()) return false;
    if (watchFilePath.empty() || backupTargets.empty()) return false;

    watching = true;
    watchThread = std::thread(&BackupManager::watchLoop, this);
    return true;
}

void BackupManager::stopWatching() {
    if (!watching.load()) return;
    watching = false;

    if (hDir != INVALID_HANDLE_VALUE) {
        CancelIoEx(hDir, NULL);
        CloseHandle(hDir);
        hDir = INVALID_HANDLE_VALUE;
    }

    if (watchThread.joinable())
        watchThread.join();
}

bool BackupManager::isWatching() const {
    return watching.load();
}

static std::wstring GetFileNameFromPath(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return path;
    return path.substr(pos + 1);
}

static bool CopyDirectoryRecursive(const std::wstring& srcDir, const std::wstring& dstDir) {
    if (!CreateDirectoryW(dstDir.c_str(), NULL)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }
    }

    WIN32_FIND_DATAW ffd;
    std::wstring searchPath = srcDir + L"\\*";
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    do {
        const std::wstring name = ffd.cFileName;
        if (name == L"." || name == L"..") continue;

        std::wstring srcPath = srcDir + L"\\" + name;
        std::wstring dstPath = dstDir + L"\\" + name;

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!CopyDirectoryRecursive(srcPath, dstPath)) {
                FindClose(hFind);
                return false;
            }
        } else {
            if (!CopyFileW(srcPath.c_str(), dstPath.c_str(), FALSE)) {
                FindClose(hFind);
                return false;
            }
        }
    } while (FindNextFileW(hFind, &ffd) != 0);

    FindClose(hFind);
    return true;
}

void BackupManager::backupFile() {
    try {
        auto timestamp = getTimestamp();
        std::filesystem::path srcPath(watchFilePath);

        DWORD attr = GetFileAttributesW(watchFilePath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            log(L"[错误] 源路径无效或不存在: " + watchFilePath);
            return;
        }

        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            for (const auto& targetDir : backupTargets) {
                std::filesystem::create_directories(targetDir);
                std::wstring folderName = GetFileNameFromPath(watchFilePath);
                std::wstring destFolder = targetDir + L"\\" + folderName + L"_" + timestamp;
                if (CopyDirectoryRecursive(watchFilePath, destFolder)) {
                    log(L"[备份成功] 文件夹 " + watchFilePath + L" -> " + destFolder);
                } else {
                    log(L"[错误] 文件夹备份失败: " + watchFilePath + L" -> " + destFolder);
                }
            }
        } else {
            for (const auto& targetDir : backupTargets) {
                std::filesystem::create_directories(targetDir);
                std::wstring backupFileName = srcPath.stem().wstring() + L"_" + timestamp + srcPath.extension().wstring();
                std::filesystem::path destPath = std::filesystem::path(targetDir) / backupFileName;
                std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing);
                log(L"[备份成功] 文件 " + destPath.wstring());
            }
        }
    }
    catch (const std::exception& e) {
        log(L"[错误] 备份失败: " + std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(e.what()));
    }
}

void BackupManager::log(const std::wstring& msg) {
    std::wofstream logFile("backup.log", std::ios::app);
    if (logFile) {
        std::wstring timeStr = getTimestamp();
        logFile << timeStr << L" " << msg << L"\n";
    }
}

std::wstring BackupManager::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    struct tm localTime;
    localtime_s(&localTime, &timeT);
    wchar_t buffer[32];
    wcsftime(buffer, sizeof(buffer) / sizeof(wchar_t), L"%Y%m%d_%H%M%S", &localTime);
    return std::wstring(buffer);
}

std::wstring toLower(const std::wstring& s) {
    std::wstring ret = s;
    std::transform(ret.begin(), ret.end(), ret.begin(),
        [](wchar_t c) { return std::towlower(c); });
    return ret;
}

void BackupManager::watchLoop() {
    if (pollingMode) {
        std::filesystem::file_time_type lastWrite = {};
        while (watching) {
            try {
                auto currentWrite = std::filesystem::last_write_time(watchFilePath);
                if (currentWrite != lastWrite) {
                    lastWrite = currentWrite;
                    log(L"[轮询] 检测到文件变化，开始备份");
                    backupFile();
                }
            } catch (...) {}
            std::this_thread::sleep_for(std::chrono::milliseconds(pollingInterval));
        }
        return;
    }

    hDir = CreateFileW(
        watchDir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);

    if (hDir == INVALID_HANDLE_VALUE) {
        log(L"[错误] 无法打开目录句柄: " + watchDir);
        watching = false;
        return;
    }

    const DWORD bufLen = 1024 * 10;
    BYTE buffer[bufLen];
    DWORD bytesReturned;

    const DWORD notifyFilter =
        FILE_NOTIFY_CHANGE_LAST_WRITE |
        FILE_NOTIFY_CHANGE_SIZE |
        FILE_NOTIFY_CHANGE_CREATION;

    const std::wstring watchFileNameLower = toLower(watchFileName);

    while (watching) {
        BOOL success = ReadDirectoryChangesW(
            hDir,
            buffer,
            bufLen,
            FALSE,
            notifyFilter,
            &bytesReturned,
            NULL,
            NULL);

        if (!success) {
            if (watching) {
                log(L"[错误] 读取目录变化失败");
            }
            break;
        }

        if (bytesReturned == 0) continue;

        DWORD offset = 0;
        while (offset < bytesReturned) {
            FILE_NOTIFY_INFORMATION* fni = (FILE_NOTIFY_INFORMATION*)(buffer + offset);
            std::wstring changedName(fni->FileName, fni->FileNameLength / sizeof(WCHAR));
            std::wstring changedNameLower = toLower(changedName);

            log(L"[事件] 文件: " + changedName + L", 动作: " + std::to_wstring(fni->Action));

            if ((fni->Action == FILE_ACTION_MODIFIED ||
                 fni->Action == FILE_ACTION_ADDED ||
                 fni->Action == FILE_ACTION_RENAMED_NEW_NAME) &&
                changedNameLower == watchFileNameLower) {
                log(L"[事件] 匹配到目标文件改动，开始备份: " + changedName);
                backupFile();
            }

            if (fni->NextEntryOffset == 0) break;
            offset += fni->NextEntryOffset;
        }
    }

    CloseHandle(hDir);
    hDir = INVALID_HANDLE_VALUE;
    watching = false;
}
