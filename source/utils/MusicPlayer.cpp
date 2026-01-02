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
    
    // 先尝试读取ID3v2标签
    std::string title = ReadID3Title(mCurrentFilePath);
    FileLogger::GetInstance().LogInfo("[MusicPlayer] GetCurrentTrackName: path=%s, ID3v2title='%s'", 
                                       mCurrentFilePath.c_str(), title.c_str());
    if (!title.empty()) {
        return title;
    }
    
    // 尝试读取ID3v1标签
    std::string artist;
    if (ReadID3v1Tag(mCurrentFilePath, title, artist)) {
        FileLogger::GetInstance().LogInfo("[MusicPlayer] Found ID3v1 title: '%s'", title.c_str());
        if (!title.empty()) {
            return title;
        }
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
    
    FileLogger::GetInstance().LogInfo("[MusicPlayer] No ID3 tag, using filename: %s", filename.c_str());
    return filename;
}

std::string MusicPlayer::GetCurrentArtist() const {
    if (mCurrentFilePath.empty()) {
        return "";
    }
    
    // 先尝试读取ID3v2标签的艺术家信息
    std::string artist = ReadID3Artist(mCurrentFilePath);
    FileLogger::GetInstance().LogInfo("[MusicPlayer] GetCurrentArtist: path=%s, ID3v2artist='%s'", 
                                       mCurrentFilePath.c_str(), artist.c_str());
    if (!artist.empty()) {
        return artist;
    }
    
    // 尝试读取ID3v1标签
    std::string title;
    if (ReadID3v1Tag(mCurrentFilePath, title, artist)) {
        FileLogger::GetInstance().LogInfo("[MusicPlayer] Found ID3v1 artist: '%s'", artist.c_str());
        return artist;
    }
    
    return "";
}

// 读取MP3文件的ID3v2标签标题
std::string MusicPlayer::ReadID3Title(const std::string& filepath) const {
    FILE* file = fopen(filepath.c_str(), "rb");
    if (!file) {
        FileLogger::GetInstance().LogWarning("[ID3] Failed to open file: %s", filepath.c_str());
        return "";
    }
    
    // 读取ID3v2头部(10字节)
    unsigned char header[10];
    if (fread(header, 1, 10, file) != 10) {
        fclose(file);
        FileLogger::GetInstance().LogWarning("[ID3] Failed to read header from: %s", filepath.c_str());
        return "";
    }
    
    // 输出前10个字节的十六进制用于调试
    FileLogger::GetInstance().LogInfo("[ID3] First 10 bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X", 
        header[0], header[1], header[2], header[3], header[4], 
        header[5], header[6], header[7], header[8], header[9]);
    FileLogger::GetInstance().LogInfo("[ID3] As text: '%c%c%c' version %d.%d", 
        header[0], header[1], header[2], header[3], header[4]);
    
    // 检查ID3v2标识 "ID3"
    if (header[0] != 'I' || header[1] != 'D' || header[2] != '3') {
        fclose(file);
        FileLogger::GetInstance().LogInfo("[ID3] No ID3v2 tag found in: %s", filepath.c_str());
        return "";
    }
    
    FileLogger::GetInstance().LogInfo("[ID3] Found ID3v2.%d tag in: %s", header[3], filepath.c_str());
    
    // 获取标签大小(使用同步安全整数,每字节只用7位)
    int tagSize = ((header[6] & 0x7F) << 21) |
                  ((header[7] & 0x7F) << 14) |
                  ((header[8] & 0x7F) << 7) |
                  (header[9] & 0x7F);
    
    FileLogger::GetInstance().LogInfo("[ID3] Tag size: %d bytes", tagSize);
    
    // 读取整个标签数据
    unsigned char* tagData = new unsigned char[tagSize];
    if (fread(tagData, 1, tagSize, file) != (size_t)tagSize) {
        delete[] tagData;
        fclose(file);
        FileLogger::GetInstance().LogWarning("[ID3] Failed to read tag data");
        return "";
    }
    fclose(file);
    
    // 解析帧来查找TIT2(标题)帧
    std::string title;
    int offset = 0;
    int frameCount = 0;
    
    // ID3v2.3和v2.4的帧ID是4字节
    while (offset + 10 < tagSize) {
        // 帧头: 4字节ID + 4字节大小 + 2字节标志
        char frameID[5] = {0};
        memcpy(frameID, tagData + offset, 4);
        
        // 如果遇到填充(00 00 00 00),停止解析
        if (frameID[0] == 0) {
            FileLogger::GetInstance().LogInfo("[ID3] Reached padding at offset %d, parsed %d frames", offset, frameCount);
            break;
        }
        
        frameCount++;
        if (frameCount <= 10) {  // 只记录前10个帧
            FileLogger::GetInstance().LogInfo("[ID3] Frame #%d: ID='%s' at offset %d", frameCount, frameID, offset);
        }
        
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
        
        if (frameCount <= 10) {
            FileLogger::GetInstance().LogInfo("[ID3]   Frame size: %d bytes", frameSize);
        }
        
        // 检查帧大小是否合理
        if (frameSize <= 0 || frameSize > tagSize - offset - 10) {
            FileLogger::GetInstance().LogWarning("[ID3] Invalid frame size %d at offset %d, stopping parse", frameSize, offset);
            break;
        }
        
        // 检查是否是TIT2(标题)帧
        if (strcmp(frameID, "TIT2") == 0 && frameSize > 1) {
            // 跳过帧头(10字节)和编码字节(1字节)
            int textStart = offset + 10 + 1;
            int textSize = frameSize - 1;
            
            unsigned char encoding = tagData[offset + 10];
            FileLogger::GetInstance().LogInfo("[ID3] Found TIT2 frame, encoding: %d, text size: %d", encoding, textSize);
            
            // 确保不越界
            if (textStart + textSize <= tagSize) {
                // 提取文本(假设是UTF-8或ISO-8859-1)
                title = std::string((char*)(tagData + textStart), textSize);
                
                // 移除可能的空字符
                size_t nullPos = title.find('\0');
                if (nullPos != std::string::npos) {
                    title = title.substr(0, nullPos);
                }
                
                FileLogger::GetInstance().LogInfo("[ID3] Found TIT2 (Title): '%s'", title.c_str());
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

// 读取MP3文件的ID3v2标签艺术家
std::string MusicPlayer::ReadID3Artist(const std::string& filepath) const {
    FILE* file = fopen(filepath.c_str(), "rb");
    if (!file) {
        FileLogger::GetInstance().LogWarning("[ID3] Failed to open file for artist: %s", filepath.c_str());
        return "";
    }
    
    // 读取ID3v2头部(10字节)
    unsigned char header[10];
    if (fread(header, 1, 10, file) != 10) {
        fclose(file);
        return "";
    }
    
    // 检查ID3v2标识 "ID3"
    if (header[0] != 'I' || header[1] != 'D' || header[2] != '3') {
        fclose(file);
        return "";
    }
    
    FileLogger::GetInstance().LogInfo("[ID3] Reading artist from ID3v2.%d tag", header[3]);
    
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
    
    // 解析帧来查找TPE1(艺术家)帧
    std::string artist;
    int offset = 0;
    int frameCount = 0;
    
    // ID3v2.3和v2.4的帧ID是4字节
    while (offset + 10 < tagSize) {
        // 帧头: 4字节ID + 4字节大小 + 2字节标志
        char frameID[5] = {0};
        memcpy(frameID, tagData + offset, 4);
        
        // 如果遇到填充(00 00 00 00),停止解析
        if (frameID[0] == 0) {
            FileLogger::GetInstance().LogInfo("[ID3] Artist search: reached padding at offset %d after %d frames", offset, frameCount);
            break;
        }
        
        frameCount++;
        
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
        
        // 检查帧大小是否合理
        if (frameSize <= 0 || frameSize > tagSize - offset - 10) {
            FileLogger::GetInstance().LogWarning("[ID3] Invalid frame size %d at offset %d, stopping", frameSize, offset);
            break;
        }
        
        // 记录前几个帧
        if (frameCount <= 10) {
            FileLogger::GetInstance().LogInfo("[ID3] Artist search frame #%d: '%s' (size=%d)", frameCount, frameID, frameSize);
        }
        
        // 检查是否是TPE1(艺术家)帧
        if (strcmp(frameID, "TPE1") == 0 && frameSize > 1) {
            // 跳过帧头(10字节)和编码字节(1字节)
            int textStart = offset + 10 + 1;
            int textSize = frameSize - 1;
            
            unsigned char encoding = tagData[offset + 10];
            FileLogger::GetInstance().LogInfo("[ID3] Found TPE1 frame, encoding: %d, text size: %d", encoding, textSize);
            
            // 确保不越界
            if (textStart + textSize <= tagSize) {
                // 提取文本(假设是UTF-8或ISO-8859-1)
                artist = std::string((char*)(tagData + textStart), textSize);
                
                // 移除可能的空字符
                size_t nullPos = artist.find('\0');
                if (nullPos != std::string::npos) {
                    artist = artist.substr(0, nullPos);
                }
                
                FileLogger::GetInstance().LogInfo("[ID3] Found TPE1 (Artist): '%s'", artist.c_str());
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
    return artist;
}

// 读取MP3文件的ID3v1标签 (文件末尾128字节)
// ID3v1格式: TAG(3) + Title(30) + Artist(30) + Album(30) + Year(4) + Comment(30) + Genre(1)
bool MusicPlayer::ReadID3v1Tag(const std::string& filepath, std::string& title, std::string& artist) const {
    FILE* file = fopen(filepath.c_str(), "rb");
    if (!file) {
        return false;
    }
    
    // 移动到文件末尾前128字节
    if (fseek(file, -128, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    
    // 读取128字节的ID3v1标签
    unsigned char tag[128];
    if (fread(tag, 1, 128, file) != 128) {
        fclose(file);
        return false;
    }
    fclose(file);
    
    // 检查 "TAG" 标识
    if (tag[0] != 'T' || tag[1] != 'A' || tag[2] != 'G') {
        FileLogger::GetInstance().LogInfo("[ID3] No ID3v1 tag found in: %s", filepath.c_str());
        return false;
    }
    
    FileLogger::GetInstance().LogInfo("[ID3] Found ID3v1 tag in: %s", filepath.c_str());
    
    // 提取标题 (字节 3-32, 30字节)
    char titleBuf[31] = {0};
    memcpy(titleBuf, tag + 3, 30);
    // 去掉尾部空格和空字符
    for (int i = 29; i >= 0; i--) {
        if (titleBuf[i] == ' ' || titleBuf[i] == '\0') {
            titleBuf[i] = '\0';
        } else {
            break;
        }
    }
    title = std::string(titleBuf);
    
    // 提取艺术家 (字节 33-62, 30字节)
    char artistBuf[31] = {0};
    memcpy(artistBuf, tag + 33, 30);
    // 去掉尾部空格和空字符
    for (int i = 29; i >= 0; i--) {
        if (artistBuf[i] == ' ' || artistBuf[i] == '\0') {
            artistBuf[i] = '\0';
        } else {
            break;
        }
    }
    artist = std::string(artistBuf);
    
    FileLogger::GetInstance().LogInfo("[ID3v1] Title: '%s', Artist: '%s'", title.c_str(), artist.c_str());
    
    return true;
}
