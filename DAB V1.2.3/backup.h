#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <windows.h>
#include <map>
#include <filesystem>

class BackupManager {
public:
    BackupManager();
    ~BackupManager();

    std::map<std::wstring, std::filesystem::file_time_type> lastFolderSnapshot;

    bool pollingMode = false;       // 是否启用轮询模式（U盘监听）
    int pollingInterval = 3000; 

    void setWatchFile(const std::wstring& fullPath);
    void addBackupTarget(const std::wstring& targetDir);
    void clearBackupTargets();

    bool startWatching();
    void stopWatching();
    bool isWatching() const;

    void backupFile(); // 立即执行一次备份

    void setPollingMode(bool enabled);       // 启用或禁用轮询模式（用于U盘）
    void setPollingInterval(int milliseconds); // 设置轮询时间间隔

private:
    void watchLoop();       // 标准的目录事件监听线程
    void pollingLoop();     // 用于轮询模式的线程
    void log(const std::wstring& msg);
    std::wstring getTimestamp();

    std::wstring watchFilePath;
    std::wstring watchDir;
    std::wstring watchFileName;

    std::vector<std::wstring> backupTargets;

    std::thread watchThread;
    std::atomic<bool> watching;

    HANDLE hDir = INVALID_HANDLE_VALUE;

    std::wstring lastKnownHash;     // 上一次文件内容的哈希值，用于检测内容变化
};
