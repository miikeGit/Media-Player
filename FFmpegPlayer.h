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

    void SetCurrentTime(double time) override;

private:
    winrt::com_ptr<ID3D11Texture2D> m_videoTexture;

    AVFormatContext* m_formatContext = nullptr;
    AVCodecContext* m_videoCodecContext = nullptr;
    SwsContext* m_swsContext = nullptr;
    int m_videoStreamIndex = -1;

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
    std::atomic<double> m_playbackSpeed{ 2.0 };
    std::vector<float> m_swrTempBuf;

    std::atomic<bool> m_isPlaying = false;
    std::atomic<bool> m_shouldSeek = false;
    std::atomic<bool> m_isStopping = false;
    std::atomic<double> m_seekTarget = 0.0;

    PacketQueue m_videoQueue;
    PacketQueue m_audioQueue;
    std::thread m_readThread;
    std::thread m_videoThread;
    std::thread m_audioThread;
    std::mutex m_frameMutex;
    std::mutex m_controlMutex;
    std::condition_variable m_controlCV;

    void FindVideoCodec();
    void FindAudioCodec();
    void FindCodecs();
    void ReadThreadFunc();
    void VideoThreadFunc();
    void AudioThreadFunc();
    void CleanupFFmpeg();
    void InitializeAudio();
    void DecodeAudioFrame(AVFrame* frame);
    void CreateD3D11Texture2DDesc();
    void ApplyMatrixTransform();
    void CheckIfSeeking();
    void CheckIfPaused(std::chrono::nanoseconds& pauseDuration);
};