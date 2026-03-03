#pragma once

#include "IPlayer.h"
#include "PacketQueue.h"
#include <SoundTouch/SoundTouch.h>

class FFmpegPlayer : public IPlayer {
public:
    FFmpegPlayer();
    ~FFmpegPlayer();

    FFmpegPlayer(const FFmpegPlayer&) = delete;
    FFmpegPlayer& operator=(const FFmpegPlayer&) = delete;

    void OpenAndPlay(const winrt::hstring& path) override;
    void RenderFrame() override;
    void Resize(UINT width, UINT height) override;
    void Play() override;
    void Pause() override;
    void Stop() override;
    void SetVolume(double volume) override;
    void SetPlaybackSpeed(double speed) override;

    double GetCurrentTime() const override;
    double GetDuration() const override;
    
    std::wstring GetCurrentSubtitle(double currentTime) override;

    void SetCurrentTime(double time) override;
    void TakeScreenshot() override;

private:
    std::filesystem::path m_currentMediaPath;

    winrt::com_ptr<ID3D11Texture2D> m_videoTexture;

    AVFormatContext* m_formatContext = nullptr;
    AVCodecContext* m_videoCodecContext = nullptr;
    SwsContext* m_swsContext = nullptr;
    int m_videoStreamIndex = -1;

    int m_subtitleStreamIndex = -1;
    AVCodecContext* m_subtitleCodecContext = nullptr;
    std::vector<SubItem> m_embeddedSubtitles;

    uint8_t* m_frameBuffer = nullptr;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    UINT m_displayWidth = 0;
    UINT m_displayHeight = 0;

    AVCodecContext* m_audioCodecContext = nullptr;
    SwrContext* m_swrContext = nullptr;
    int m_audioStreamIndex = -1;
    int m_audioSampleRate = 0;
    int m_audioChannels = 0;

    winrt::com_ptr<IXAudio2> m_xaudio2;
    IXAudio2MasteringVoice* m_masteringVoice = nullptr;
    IXAudio2SourceVoice* m_sourceVoice = nullptr;

    constexpr static int AUDIO_BUFFER_COUNT = 4;
    std::array<std::vector<float>, AUDIO_BUFFER_COUNT> m_audioBufferPool;
    int m_audioPoolIndex = 0;
    double m_currentTime{ 0.0 };
    double m_duration = 0.0;

    soundtouch::SoundTouch m_soundTouch;
    std::atomic<double> m_seekTarget = 0.0;
    std::atomic<double> m_playbackSpeed{ 1.0 };
    std::vector<float> m_swrTempBuf;

    std::atomic<bool> m_isPlaying = false;
    std::atomic<bool> m_shouldSeek = false;
    std::atomic<bool> m_isStopping = false;
    std::atomic<bool> m_videoSpeedChanged = false;
    std::atomic<bool> m_audioSpeedChanged = false;

    PacketQueue m_videoQueue;
    PacketQueue m_audioQueue;
    PacketQueue m_subtitleQueue;
    std::thread m_readThread;
    std::thread m_videoThread;
    std::thread m_audioThread;
    std::thread m_subtitleThread;
    std::mutex m_frameMutex;
    std::mutex m_controlMutex;
    std::mutex m_subtitleMutex;
    std::condition_variable m_controlCV;

    void FindSubtitleCodec();
    void FindVideoCodec();
    void FindAudioCodec();
    void FindCodecs();
    void ReadThreadFunc();
    void VideoThreadFunc();
    void AudioThreadFunc();
    void SubtitleThreadFunc();
    void CleanupFFmpeg();
    void InitializeAudio();
    void DecodeSubtitlePacket(AVPacket* packet);
    void DecodeAudioFrame(AVFrame* frame);
    void CreateD3D11Texture2DDesc();
    void ApplyMatrixTransform();
    void CheckIfSeeking();
    void CheckIfPaused(std::chrono::nanoseconds& pauseDuration);
};