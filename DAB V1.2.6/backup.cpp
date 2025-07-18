#include "backup.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <windows.h>
#include <algorithm>
#include <codecvt>
#include <processthreadsapi.h>

BackupManager::BackupManager() : watching(false), hDir(INVALID_HANDLE_VALUE), pollingMode(false), pollingInterval(3000) {}

BackupManager::~BackupManager() {
    stopWatching();
}

void BackupManager::setMaxBackupCount(int count) {
    maxBackupCount = count;
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

    lastFolderSnapshot.clear();
    watching = true;

    watchThread = std::make_unique<std::thread>(&BackupManager::watchLoop, this);
    return true;
}

void BackupManager::stopWatching() {
    if (!watching.load()) return;

    watching = false;

    // 唤醒轮询线程（如果在 sleep 中）
    cv.notify_all();

    if (watchThread && watchThread->joinable()) {
        if (!pollingMode && hDir != INVALID_HANDLE_VALUE) {
            // 普通监听模式下，打断 ReadDirectoryChangesW 阻塞
            auto nativeHandle = watchThread->native_handle(); // uintptr_t 类型
            DWORD threadId = GetThreadId(reinterpret_cast<HANDLE>(nativeHandle));
            if (threadId != 0) {
                HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, threadId);
                if (hThread) {
                    CancelSynchronousIo(hThread); // 中断 ReadDirectoryChangesW
                    CloseHandle(hThread);
                }
            }
        }

        watchThread->join();
        watchThread.reset();
    }

    if (hDir != INVALID_HANDLE_VALUE) {
        CloseHandle(hDir);
        hDir = INVALID_HANDLE_VALUE;
    }
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
        }
        else {
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

        std::wstring baseName = GetFileNameFromPath(watchFilePath);
        std::wstring backupSubfolderName = baseName + L" Backup";

        for (const auto& targetDir : backupTargets) {
            std::filesystem::path backupFolder = std::filesystem::path(targetDir) / backupSubfolderName;
            std::filesystem::create_directories(backupFolder);

            if (attr & FILE_ATTRIBUTE_DIRECTORY) {
                std::filesystem::path destFolder = backupFolder / (baseName + L"_" + timestamp);
                if (CopyDirectoryRecursive(watchFilePath, destFolder.wstring())) {
                    log(L"[备份成功] 文件夹 " + watchFilePath + L" -> " + destFolder.wstring());
                } else {
                    log(L"[错误] 文件夹备份失败: " + watchFilePath + L" -> " + destFolder.wstring());
                }
            } else {
                std::wstring backupFileName = srcPath.stem().wstring() + L"_" + timestamp + srcPath.extension().wstring();
                std::filesystem::path destPath = backupFolder / backupFileName;
                std::filesystem::copy_file(srcPath, destPath, std::filesystem::copy_options::overwrite_existing);
                log(L"[备份成功] 文件 " + destPath.wstring());
            }

            // 控制备份数量
            std::vector<std::filesystem::directory_entry> entries;
            for (const auto& entry : std::filesystem::directory_iterator(backupFolder)) {
                if (entry.is_regular_file() || entry.is_directory()) {
                    entries.push_back(entry);
                }
            }

            std::sort(entries.begin(), entries.end(),
                      [](const std::filesystem::directory_entry& a, const std::filesystem::directory_entry& b) {
                          return a.last_write_time() < b.last_write_time();
                      });

            while ((int)entries.size() > maxBackupCount) {
                try {
                    const auto& oldest = entries.front();
                    std::filesystem::remove_all(oldest);
                    log(L"[清理] 删除旧备份: " + oldest.path().wstring());
                    entries.erase(entries.begin());
                } catch (...) {
                    log(L"[警告] 删除旧备份失败: " + entries.front().path().wstring());
                    entries.erase(entries.begin());
                }
            }
        }
    } catch (const std::exception& e) {
        log(L"[错误] 备份失败: " + std::wstring_convert<std::codecvt_utf8<wchar_t>>().from_bytes(e.what()));
    }
}

void BackupManager::log(const std::wstring& msg) {
    // 打开文件（如果文件不存在，会创建；存在则追加）
    std::wofstream logFile("backup.log", std::ios::app);
    if (!logFile) return;

    // 设置输出 locale 为 UTF-8，确保中文写入正确（使用 BOM）
    static std::locale utf8_locale(std::locale(), new std::codecvt_utf8<wchar_t>);
    logFile.imbue(utf8_locale);

    std::time_t t = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &t);
    wchar_t timebuf[64];
    wcsftime(timebuf, 64, L"[%Y-%m-%d %H:%M:%S] ", &local);
    std::wstring fullMsg = timebuf + msg + L"\r\n";

    logFile << fullMsg;
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
                DWORD attr = GetFileAttributesW(watchFilePath.c_str());
                if (attr == INVALID_FILE_ATTRIBUTES) {
                    log(L"[轮询] 源路径无效或不存在: " + watchFilePath);
                    break;
                }

                if (attr & FILE_ATTRIBUTE_DIRECTORY) {
                    bool changed = false;
                    std::map<std::wstring, std::filesystem::file_time_type> newSnapshot;

                    for (auto& p : std::filesystem::recursive_directory_iterator(watchFilePath)) {
                        if (std::filesystem::is_regular_file(p)) {
                            auto path = p.path().wstring();
                            auto ftime = std::filesystem::last_write_time(p);
                            newSnapshot[path] = ftime;

                            auto it = lastFolderSnapshot.find(path);
                            if (it == lastFolderSnapshot.end() || it->second != ftime) {
                                changed = true;
                            }
                        }
                    }

                    if (changed) {
                        log(L"[轮询] 检测到文件夹中文件变更，开始备份");
                        backupFile();
                    }

                    lastFolderSnapshot = std::move(newSnapshot);
                } else {
                    auto currentWrite = std::filesystem::last_write_time(watchFilePath);
                    if (currentWrite != lastWrite) {
                        lastWrite = currentWrite;
                        log(L"[轮询] 检测到文件变化，开始备份");
                        backupFile();
                    }
                }
            } catch (...) {
                log(L"[轮询] 检查文件状态时出错");
            }

            std::unique_lock<std::mutex> lk(cv_mtx);
            cv.wait_for(lk, std::chrono::milliseconds(pollingInterval), [this]() { return !watching.load(); });

            if (!watching) break;
        }
        return;
    }

    // === 异步非轮询模式 ===

    hDir = CreateFileW(
        watchDir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, // 必须启用异步标志
        NULL);

    if (hDir == INVALID_HANDLE_VALUE) {
        log(L"[错误] 无法打开目录句柄: " + watchDir);
        watching = false;
        return;
    }

    const DWORD bufLen = 1024 * 10;
    BYTE buffer[bufLen];

    const DWORD notifyFilter =
        FILE_NOTIFY_CHANGE_LAST_WRITE |
        FILE_NOTIFY_CHANGE_SIZE |
        FILE_NOTIFY_CHANGE_CREATION;

    const std::wstring watchFileNameLower = toLower(watchFileName);

    OVERLAPPED overlapped = {};
    HANDLE hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    overlapped.hEvent = hEvent;

    while (watching) {
        ResetEvent(hEvent);

        BOOL success = ReadDirectoryChangesW(
            hDir,
            buffer,
            bufLen,
            FALSE,             // 不递归子目录
            notifyFilter,
            NULL,              // 让它异步填充
            &overlapped,
            NULL);

        if (!success) {
            log(L"[错误] 启动 ReadDirectoryChangesW 异步监听失败");
            break;
        }

        DWORD waitResult = WaitForSingleObject(hEvent, 500);  // 最多等待500ms

        if (!watching) break;

        if (waitResult == WAIT_OBJECT_0) {
            DWORD bytesReturned = 0;
            if (!GetOverlappedResult(hDir, &overlapped, &bytesReturned, FALSE)) {
                log(L"[错误] GetOverlappedResult 失败");
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
        } else if (waitResult == WAIT_TIMEOUT) {
            continue;  // 继续下一轮监听
        } else {
            log(L"[错误] WaitForSingleObject 异常");
            break;
        }
    }

    CloseHandle(hEvent);
    CloseHandle(hDir);
    hDir = INVALID_HANDLE_VALUE;
    watching = false;
}