#include "MusicPlayer.hpp"
#include "Config.hpp"
#include "FileLogger.hpp"
#include <SDL2/SDL.h>
#include <cstring>
#include <cstdio>

MusicPlayer::MusicPlayer() 
    : mMusic(nullptr)
    , mVolume(32)  // 默认25%音量
    , mEnabled(true)
    , mInitialized(false)
    , mWasEnabled(true)
    , mCurrentFilePath("") {
}

MusicPlayer::~MusicPlayer() {
    Shutdown();
}

MusicPlayer& MusicPlayer::GetInstance() {
    static MusicPlayer instance;
    return instance;
}

bool MusicPlayer::Init() {
    if (mInitialized) {
        return true;
    }
    
    FileLogger::GetInstance().LogInfo("MusicPlayer: Initializing SDL2_mixer...");
    
    // 初始化SDL音频子系统(如果还没初始化)
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            FileLogger::GetInstance().LogError("MusicPlayer: Failed to init SDL audio: %s", SDL_GetError());
            return false;
        }
    }
    
    // 初始化SDL2_mixer
    // 参数: 频率, 格式, 声道数, 块大小
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        FileLogger::GetInstance().LogError("MusicPlayer: Mix_OpenAudio failed: %s", Mix_GetError());
        return false;
    }
    
    // 分配混音通道数
    Mix_AllocateChannels(16);
    
    mInitialized = true;
    FileLogger::GetInstance().LogInfo("MusicPlayer: Initialized successfully");
    return true;
}

void MusicPlayer::Shutdown() {
    if (!mInitialized) {
        return;
    }
    
    FileLogger::GetInstance().LogInfo("MusicPlayer: Shutting down...");
    
    Stop();
    
    if (mMusic) {
        Mix_FreeMusic(mMusic);
        mMusic = nullptr;
    }
    
    Mix_CloseAudio();
    
    mInitialized = false;
    FileLogger::GetInstance().LogInfo("MusicPlayer: Shutdown complete");
}

bool MusicPlayer::LoadMusic(const std::string& filepath) {
    if (!mInitialized) {
        FileLogger::GetInstance().LogError("MusicPlayer: Not initialized");
        return false;
    }
    
    FileLogger::GetInstance().LogInfo("MusicPlayer: Loading music from %s", filepath.c_str());
    
    // 释放之前的音乐
    if (mMusic) {
        Mix_FreeMusic(mMusic);
        mMusic = nullptr;
    }
    
    // 加载新音乐
    mMusic = Mix_LoadMUS(filepath.c_str());
    if (!mMusic) {
        FileLogger::GetInstance().LogError("MusicPlayer: Failed to load music: %s", Mix_GetError());
        mCurrentFilePath = "";
        return false;
    }
    
    // 保存文件路径
    mCurrentFilePath = filepath;
    
    FileLogger::GetInstance().LogInfo("MusicPlayer: Music loaded successfully");
    return true;
}

void MusicPlayer::Play() {
    if (!mInitialized || !mMusic || !mEnabled) {
        return;
    }
    
    if (!IsPlaying()) {
        FileLogger::GetInstance().LogInfo("MusicPlayer: Starting music playback");
        Mix_PlayMusic(mMusic, -1);  // -1 = 循环播放
        Mix_VolumeMusic(mVolume);
    }
}

void MusicPlayer::Stop() {
    if (!mInitialized) {
        return;
    }
    
    if (IsPlaying()) {
        FileLogger::GetInstance().LogInfo("MusicPlayer: Stopping music");
        Mix_HaltMusic();
    }
}

void MusicPlayer::Pause() {
    if (!mInitialized || !IsPlaying()) {
        return;
    }
    
    FileLogger::GetInstance().LogInfo("MusicPlayer: Pausing music");
    Mix_PauseMusic();
}

void MusicPlayer::Resume() {
    if (!mInitialized || !IsPaused()) {
        return;
    }
    
    FileLogger::GetInstance().LogInfo("MusicPlayer: Resuming music");
    Mix_ResumeMusic();
}

void MusicPlayer::SetVolume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 128) volume = 128;
    
    mVolume = volume;
    
    if (mInitialized) {
        Mix_VolumeMusic(mVolume);
    }
}

void MusicPlayer::SetEnabled(bool enabled) {
    if (mEnabled != enabled) {
        FileLogger::GetInstance().LogInfo("MusicPlayer: %s", enabled ? "Enabled" : "Disabled");
        mEnabled = enabled;
        
        if (enabled) {
            Play();
        } else {
            Stop();
        }
    }
}

bool MusicPlayer::IsPlaying() const {
    if (!mInitialized) {
        return false;
    }
    return Mix_PlayingMusic() == 1 && !IsPaused();
}

bool MusicPlayer::IsPaused() const {
    if (!mInitialized) {
        return false;
    }
    return Mix_PausedMusic() == 1;
}

std::string MusicPlayer::GetCurrentTrackName() const {
    if (mCurrentFilePath.empty()) {
        return "No Music";
    }
    
    // 尝试读取ID3标签
    std::string title = ReadID3Title(mCurrentFilePath);
    if (!title.empty()) {
        return title;
    }
    
    // 如果没有ID3标签,从路径中提取文件名
    size_t lastSlash = mCurrentFilePath.find_last_of("/\\");
    std::string filename;
    if (lastSlash != std::string::npos) {
        filename = mCurrentFilePath.substr(lastSlash + 1);
    } else {
        filename = mCurrentFilePath;
    }
    
    // 去掉扩展名
    size_t lastDot = filename.find_last_of('.');
    if (lastDot != std::string::npos) {
        filename = filename.substr(0, lastDot);
    }
    
    return filename;
}

// 读取MP3文件的ID3v2标签标题
std::string MusicPlayer::ReadID3Title(const std::string& filepath) const {
    FILE* file = fopen(filepath.c_str(), "rb");
    if (!file) {
        return "";
    }
    
    // 读取ID3v2头部(10字节)
    unsigned char header[10];
    if (fread(header, 1, 10, file) != 10) {
        fclose(file);
        return "";
    }
    
    // 检查ID3v2标识 "ID3"
    if (header[0] != 'I' || header[1] != 'D' || header[2] != 'D') {
        fclose(file);
        return "";
    }
    
    // 获取标签大小(使用同步安全整数,每字节只用7位)
    int tagSize = ((header[6] & 0x7F) << 21) |
                  ((header[7] & 0x7F) << 14) |
                  ((header[8] & 0x7F) << 7) |
                  (header[9] & 0x7F);
    
    // 读取整个标签数据
    unsigned char* tagData = new unsigned char[tagSize];
    if (fread(tagData, 1, tagSize, file) != (size_t)tagSize) {
        delete[] tagData;
        fclose(file);
        return "";
    }
    fclose(file);
    
    // 解析帧来查找TIT2(标题)帧
    std::string title;
    int offset = 0;
    
    // ID3v2.3和v2.4的帧ID是4字节
    while (offset + 10 < tagSize) {
        // 帧头: 4字节ID + 4字节大小 + 2字节标志
        char frameID[5] = {0};
        memcpy(frameID, tagData + offset, 4);
        
        // 如果遇到填充(00 00 00 00),停止解析
        if (frameID[0] == 0) {
            break;
        }
        
        // 获取帧大小
        int frameSize;
        if (header[3] == 4) {  // ID3v2.4使用同步安全整数
            frameSize = ((tagData[offset + 4] & 0x7F) << 21) |
                       ((tagData[offset + 5] & 0x7F) << 14) |
                       ((tagData[offset + 6] & 0x7F) << 7) |
                       (tagData[offset + 7] & 0x7F);
        } else {  // ID3v2.3使用普通整数(大端序)
            frameSize = (tagData[offset + 4] << 24) |
                       (tagData[offset + 5] << 16) |
                       (tagData[offset + 6] << 8) |
                       tagData[offset + 7];
        }
        
        // 检查是否是TIT2(标题)帧
        if (strcmp(frameID, "TIT2") == 0 && frameSize > 1) {
            // 跳过帧头(10字节)和编码字节(1字节)
            int textStart = offset + 10 + 1;
            int textSize = frameSize - 1;
            
            // 确保不越界
            if (textStart + textSize <= tagSize) {
                // 提取文本(假设是UTF-8或ISO-8859-1)
                title = std::string((char*)(tagData + textStart), textSize);
                
                // 移除可能的空字符
                size_t nullPos = title.find('\0');
                if (nullPos != std::string::npos) {
                    title = title.substr(0, nullPos);
                }
                
                break;
            }
        }
        
        // 移动到下一帧
        offset += 10 + frameSize;
        
        // 防止无限循环
        if (frameSize == 0) {
            break;
        }
    }
    
    delete[] tagData;
    return title;
}

void MusicPlayer::Update() {
    if (!mInitialized) {
        return;
    }
    
    // 检查配置是否改变
    bool configEnabled = Config::GetInstance().IsBgmEnabled();
    if (configEnabled != mWasEnabled) {
        mWasEnabled = configEnabled;
        SetEnabled(configEnabled);
    }
    
    // 如果启用但没有播放,则开始播放
    if (mEnabled && mMusic && !IsPlaying() && !IsPaused()) {
        Play();
    }
}
