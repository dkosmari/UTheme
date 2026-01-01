#pragma once

#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <curl/curl.h>
#include <cstdio>

// BGM下载状态
enum BgmDownloadState {
    BGM_IDLE,
    BGM_DOWNLOADING,
    BGM_COMPLETE,
    BGM_ERROR,
    BGM_CANCELLED
};

// BGM下载器 - 后台线程下载防止阻塞
class BgmDownloader {
public:
    static BgmDownloader& GetInstance();
    
    // 开始下载BGM
    void StartDownload(const std::string& url);
    
    // 取消下载
    void Cancel();
    
    // 每帧更新 - 检查下载状态
    void Update();
    
    // 状态查询
    BgmDownloadState GetState() const { return mState; }
    float GetProgress() const { return mProgress; }
    const std::string& GetError() const { return mErrorMessage; }
    bool IsDownloading() const { return mState == BGM_DOWNLOADING; }
    
    // 获取下载信息
    long GetDownloadedBytes() const { return mDownloadedBytes; }
    long GetTotalBytes() const { return mTotalBytes; }
    
    // 回调设置 - 下载完成时触发
    void SetCompletionCallback(std::function<void(bool success, const std::string& filepath)> callback);
    
private:
    BgmDownloader();
    ~BgmDownloader();
    BgmDownloader(const BgmDownloader&) = delete;
    BgmDownloader& operator=(const BgmDownloader&) = delete;
    
    std::atomic<BgmDownloadState> mState;
    std::atomic<float> mProgress;
    std::atomic<long> mDownloadedBytes;
    std::atomic<long> mTotalBytes;
    std::atomic<bool> mCancelRequested;
    
    std::string mErrorMessage;
    std::string mCurrentUrl;
    std::mutex mMutex;
    std::function<void(bool success, const std::string& filepath)> mCompletionCallback;
    
    // 后台下载线程
    std::thread mDownloadThread;
    bool mThreadRunning;
    
    // 内部方法 - 在后台线程中运行
    void PerformDownload();
    
    // CURL进度回调
    static int ProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, 
                                curl_off_t ultotal, curl_off_t ulnow);
};
