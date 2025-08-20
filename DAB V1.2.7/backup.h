#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <windows.h>
#include <map>
#include <filesystem>
#include <memory>
#include <condition_variable>
#include <mutex>

class BackupManager {
public:
    BackupManager();
    ~BackupManager();

    std::unique_ptr<std::thread> watchThread;
    std::atomic<bool> watching{ false };  // 用于控制线程生命周期

    std::map<std::wstring, std::filesystem::file_time_type> lastFolderSnapshot;

    int maxBackupCount = 10;  // 默认最多保留10个备份
    bool pollingMode = false;       // 是否启用轮询模式（U盘监听）
    bool incrementalMode = false;   // 是否启用增量备份模式
    int pollingInterval = 3000;

    void setMaxBackupCount(int count);
    void setWatchFile(const std::wstring& fullPath);
    void addBackupTarget(const std::wstring& targetDir);
    void clearBackupTargets();

    bool startWatching();
    void stopWatching();
    bool isWatching() const;

    void backupFile(); // 立即执行一次备份

    void setPollingMode(bool enabled);       // 启用或禁用轮询模式（用于U盘）
    void setIncrementalMode(bool enabled);   // 启用或禁用增量备份模式
    void setPollingInterval(int milliseconds); // 设置轮询时间间隔

private:
    void watchLoop();       // 标准的目录事件监听线程

    void log(const std::wstring& msg);
    std::wstring getTimestamp();

    std::wstring watchFilePath;
    std::wstring watchDir;
    std::wstring watchFileName;

    std::vector<std::wstring> backupTargets;

    HANDLE hDir = INVALID_HANDLE_VALUE;

    std::condition_variable cv;
    std::mutex cv_mtx;
};
