#include "BgmDownloader.hpp"
#include "FileLogger.hpp"
#include "Config.hpp"
#include "MusicPlayer.hpp"
#include "../Screen.hpp"
#include <curl/curl.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

BgmDownloader& BgmDownloader::GetInstance() {
    static BgmDownloader instance;
    return instance;
}

BgmDownloader::BgmDownloader() 
    : mState(BGM_IDLE)
    , mProgress(0.0f)
    , mDownloadedBytes(0)
    , mTotalBytes(0)
    , mCancelRequested(false)
    , mThreadRunning(false) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    FileLogger::GetInstance().LogInfo("[BgmDownloader] Initialized");
}

BgmDownloader::~BgmDownloader() {
    Cancel();
    
    // 等待下载线程结束
    if (mDownloadThread.joinable()) {
        mDownloadThread.join();
    }
    
    curl_global_cleanup();
    FileLogger::GetInstance().LogInfo("[BgmDownloader] Destroyed");
}

void BgmDownloader::StartDownload(const std::string& url) {
    // 如果正在下载,先取消
    if (IsDownloading()) {
        FileLogger::GetInstance().LogInfo("[BgmDownloader] Already downloading, canceling previous download");
        Cancel();
        
        // 等待之前的线程结束
        if (mDownloadThread.joinable()) {
            mDownloadThread.join();
        }
    }
    
    mCurrentUrl = url;
    mCancelRequested.store(false);
    mState.store(BGM_DOWNLOADING);
    mProgress.store(0.0f);
    mDownloadedBytes.store(0);
    mTotalBytes.store(0);
    mErrorMessage = "";
    
    FileLogger::GetInstance().LogInfo("[BgmDownloader] Starting download from: %s", url.c_str());
    
    // 启动后台线程进行下载
    mThreadRunning = true;
    mDownloadThread = std::thread([this]() {
        PerformDownload();
        mThreadRunning = false;
    });
}

void BgmDownloader::Cancel() {
    if (IsDownloading()) {
        FileLogger::GetInstance().LogInfo("[BgmDownloader] Canceling download");
        mCancelRequested.store(true);
        mState.store(BGM_CANCELLED);
    }
}

void BgmDownloader::SetCompletionCallback(std::function<void(bool, const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    mCompletionCallback = callback;
}

void BgmDownloader::Update() {
    // Update方法现在只用于检查状态,实际下载在后台线程中进行
    // 不需要在这里做任何事情
}

// CURL进度回调函数
int BgmDownloader::ProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, 
                                    curl_off_t ultotal, curl_off_t ulnow) {
    BgmDownloader* downloader = static_cast<BgmDownloader*>(clientp);
    
    if (downloader->mCancelRequested.load()) {
        return 1; // 返回非0会中止下载
    }
    
    if (dltotal > 0) {
        downloader->mTotalBytes.store(dltotal);
        float progress = (float)dlnow / (float)dltotal;
        downloader->mProgress.store(progress);
    }
    
    return 0;
}

void BgmDownloader::PerformDownload() {
    const char* destPath = "fs:/vol/external01/UTheme/BGM.mp3";
    const char* tempPath = "fs:/vol/external01/UTheme/BGM.mp3.tmp";
    
    FileLogger::GetInstance().LogInfo("[BgmDownloader] Starting download to: %s", destPath);
    
    // 确保目录存在
    const char* dirPath = "fs:/vol/external01/UTheme";
    struct stat st;
    if (stat(dirPath, &st) != 0) {
        mkdir(dirPath, 0777);
    }
    
    // 打开临时文件
    FILE* file = fopen(tempPath, "wb");
    if (!file) {
        std::lock_guard<std::mutex> lock(mMutex);
        mErrorMessage = "Failed to create temporary file";
        FileLogger::GetInstance().LogError("[BgmDownloader] %s", mErrorMessage.c_str());
        mState.store(BGM_ERROR);
        if (mCompletionCallback) {
            mCompletionCallback(false, mErrorMessage);
        }
        return;
    }
    
    // 初始化CURL
    CURL* curl = curl_easy_init();
    
    if (!curl) {
        std::lock_guard<std::mutex> lock(mMutex);
        mErrorMessage = "Failed to initialize CURL";
        FileLogger::GetInstance().LogError("[BgmDownloader] %s", mErrorMessage.c_str());
        fclose(file);
        mState.store(BGM_ERROR);
        if (mCompletionCallback) {
            mCompletionCallback(false, mErrorMessage);
        }
        return;
    }
    
    // 配置CURL
    curl_easy_setopt(curl, CURLOPT_URL, mCurrentUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L); // 5分钟超时
    
    // 执行下载(这个调用会阻塞,但在后台线程中运行所以不会影响UI)
    CURLcode res = curl_easy_perform(curl);
    
    // 关闭文件
    fclose(file);
    
    // 清理CURL
    curl_easy_cleanup(curl);
    
    // 检查结果
    if (res != CURLE_OK) {
        std::lock_guard<std::mutex> lock(mMutex);
        mErrorMessage = curl_easy_strerror(res);
        FileLogger::GetInstance().LogError("[BgmDownloader] Download failed: %s", mErrorMessage.c_str());
        remove(tempPath);
        mState.store(BGM_ERROR);
        
        // 显示错误通知
        Screen::GetBgmNotification().ShowError("Download failed: " + mErrorMessage);
        
        if (mCompletionCallback) {
            mCompletionCallback(false, mErrorMessage);
        }
        return;
    }
    
    // 获取HTTP状态码
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    
    if (httpCode != 200) {
        std::lock_guard<std::mutex> lock(mMutex);
        mErrorMessage = "HTTP error: " + std::to_string(httpCode);
        FileLogger::GetInstance().LogError("[BgmDownloader] %s", mErrorMessage.c_str());
        remove(tempPath);
        mState.store(BGM_ERROR);
        
        // 显示错误通知
        Screen::GetBgmNotification().ShowError(mErrorMessage);
        
        if (mCompletionCallback) {
            mCompletionCallback(false, mErrorMessage);
        }
        return;
    }
    
    // 重命名临时文件
    remove(destPath); // 先删除旧文件
    if (rename(tempPath, destPath) != 0) {
        std::lock_guard<std::mutex> lock(mMutex);
        mErrorMessage = "Failed to rename temporary file";
        FileLogger::GetInstance().LogError("[BgmDownloader] %s", mErrorMessage.c_str());
        remove(tempPath);
        mState.store(BGM_ERROR);
        if (mCompletionCallback) {
            mCompletionCallback(false, mErrorMessage);
        }
        return;
    }
    
    // 下载成功
    FileLogger::GetInstance().LogInfo("[BgmDownloader] Download completed successfully");
    mState.store(BGM_COMPLETE);
    mProgress.store(1.0f);
    
    // 显示成功通知
    Screen::GetBgmNotification().ShowNowPlaying("BGM.mp3");
    
    // 尝试加载音乐
    if (MusicPlayer::GetInstance().LoadMusic(destPath)) {
        MusicPlayer::GetInstance().SetEnabled(Config::GetInstance().IsBgmEnabled());
        MusicPlayer::GetInstance().SetVolume(32);
        FileLogger::GetInstance().LogInfo("[BgmDownloader] BGM loaded and playing");
    }
    
    std::lock_guard<std::mutex> lock(mMutex);
    if (mCompletionCallback) {
        mCompletionCallback(true, "");
    }
}
