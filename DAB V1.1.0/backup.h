#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <windows.h>

class BackupManager {
public:
    BackupManager();
    ~BackupManager();

    void setWatchFile(const std::wstring& fullPath);
    void addBackupTarget(const std::wstring& targetDir);
    void clearBackupTargets();

    bool startWatching();
    void stopWatching();
    bool isWatching() const;

    // 设为public，方便从外部调用立即备份
    void backupFile();

private:
    void watchLoop();

    std::wstring watchFilePath;
    std::wstring watchDir;
    std::wstring watchFileName;

    std::vector<std::wstring> backupTargets;

    std::thread watchThread;
    std::atomic<bool> watching;

    HANDLE hDir = INVALID_HANDLE_VALUE;  // 目录句柄，用于取消阻塞监听

    void log(const std::wstring& msg);

    std::wstring getTimestamp();
};
