#pragma once

#include <SoundTouch/SoundTouch.h>

#include "PacketQueue.h"
#include "IPlayer.h"

class ArchiveClient;

enum class VideoEffect {
    Normal,
    Grayscale
};

enum class AudioEffect {
    Normal,
    Reverb
};

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;
struct SwrContext;
struct SwsContext;
struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11PixelShader;
struct ID3D11SamplerState;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;
struct AVIOContext;

struct AVFormatContextDeleter { void operator()(AVFormatContext* ctx) const; };
struct AVCodecContextDeleter { void operator()(AVCodecContext* ctx) const; };
struct SwsContextDeleter { void operator()(SwsContext* ctx) const; };
struct SwrContextDeleter { void operator()(SwrContext* ctx) const; };
struct AVPacketDeleter { void operator()(AVPacket* pkt) const; };
struct AVFrameDeleter { void operator()(AVFrame* frame) const; };
struct AVIOContextDeleter { void operator()(AVIOContext* ctx) const; };
struct AVFreeDeleter { void operator()(uint8_t* ptr) const; };
struct XAudio2MasteringVoiceDeleter { void operator()(IXAudio2MasteringVoice* voice) const; };
struct XAudio2SourceVoiceDeleter { void operator()(IXAudio2SourceVoice* voice) const; };

using MasteringVoice_ptr = std::unique_ptr<IXAudio2MasteringVoice, XAudio2MasteringVoiceDeleter>;
using SourceVoice_ptr = std::unique_ptr<IXAudio2SourceVoice, XAudio2SourceVoiceDeleter>;
using AVFormatContext_ptr = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
using AVCodecContext_ptr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using SwsContext_ptr = std::unique_ptr<SwsContext, SwsContextDeleter>;
using SwrContext_ptr = std::unique_ptr<SwrContext, SwrContextDeleter>;
using AVPacket_ptr = std::unique_ptr<AVPacket, AVPacketDeleter>;
using AVFrame_ptr = std::unique_ptr<AVFrame, AVFrameDeleter>;
using AVIOContext_ptr = std::unique_ptr<AVIOContext, AVIOContextDeleter>;
using AVMem_ptr = std::unique_ptr<uint8_t, AVFreeDeleter>;

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
    void SetCurrentTime(std::chrono::duration<double> time) override;
    bool TakeScreenshot() override;
    void StartClipRecording() override;
    bool StopClipRecording() override;
    void SetVideoEffect(VideoEffect effect);
    void SetAudioEffect(AudioEffect effect);
    void OpenFromArchive(const std::string& zipPath);
    
    bool IsClipRecording() const override;
    std::chrono::duration<double> GetCurrentTime() const override;
    std::chrono::duration<double> GetDuration() const override;
    std::wstring GetCurrentSubtitle(std::chrono::duration<double> currentTime) override;

    std::vector<uint8_t> ExtractThumbnail(std::chrono::duration<double> targetTime, int thumbWidth, int thumbHeight);
    
private:
    std::unique_ptr<ArchiveClient> m_archiveClient;
    AVIOContext_ptr m_avioContext = nullptr;
    std::filesystem::path m_currentMediaPath;

    winrt::com_ptr<ID3D11Texture2D> m_videoTexture;

    AVFormatContext_ptr m_formatContext = nullptr;
    AVCodecContext_ptr m_videoCodecContext = nullptr;
    SwsContext_ptr m_swsContext = nullptr;
    int m_videoStreamIndex = -1;

    int m_thumbnailStreamIndex = -1;
    AVFormatContext_ptr m_thumbFormatContext = nullptr;
    AVCodecContext_ptr m_thumbCodecContext = nullptr;
    SwsContext_ptr m_thumbSwsContext = nullptr;
    AVPacket_ptr m_thumbPacket = nullptr;
    AVFrame_ptr m_thumbFrame = nullptr;
    int m_lastThumbHeight;
    int m_lastThumbWidth;

    int m_subtitleStreamIndex = -1;
    AVCodecContext_ptr m_subtitleCodecContext = nullptr;
    std::vector<SubItem> m_embeddedSubtitles;

    AVMem_ptr m_frameBuffer = nullptr;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    UINT m_displayWidth = 0;
    UINT m_displayHeight = 0;

    AVCodecContext_ptr m_audioCodecContext = nullptr;
    SwrContext_ptr m_swrContext = nullptr;
    int m_audioStreamIndex = -1;
    int m_audioSampleRate = 0;
    int m_audioChannels = 0;

    winrt::com_ptr<IXAudio2> m_xaudio2;
    MasteringVoice_ptr m_masteringVoice = nullptr;
    SourceVoice_ptr m_sourceVoice = nullptr;

    constexpr static int AUDIO_BUFFER_COUNT = 4;
    std::array<std::vector<float>, AUDIO_BUFFER_COUNT> m_audioBufferPool;
    int m_audioPoolIndex = 0;
    std::chrono::duration<double> m_currentTime{ 0.0 };
    std::chrono::duration<double> m_duration{ 0.0 };
    std::chrono::duration<double> m_clipStartTime{ 0.0 };

    soundtouch::SoundTouch m_soundTouch;
    std::atomic<double> m_seekTarget = 0.0;
    std::atomic<double> m_playbackSpeed = 1.0;
    std::vector<float> m_swrTempBuf;

    std::atomic<bool> m_isPlaying = false;
    std::atomic<bool> m_shouldSeek = false;
    std::atomic<bool> m_isStopping = false;
    std::atomic<bool> m_videoSpeedChanged = false;
    std::atomic<bool> m_audioSpeedChanged = false;
    std::atomic<bool> m_isClipRecording = false;

    PacketQueue m_videoQueue;
    PacketQueue m_audioQueue;
    PacketQueue m_subtitleQueue;
    std::thread m_readThread;
    std::thread m_videoThread;
    std::thread m_audioThread;
    std::thread m_subtitleThread;
    std::thread m_clipExportThread;
    std::mutex m_frameMutex;
    std::mutex m_controlMutex;
    std::mutex m_thumbnailMutex;
    std::condition_variable m_controlCV;

    winrt::com_ptr<ID3D11VertexShader> m_vertexShader;
    winrt::com_ptr<ID3D11PixelShader> m_psNormal;
    winrt::com_ptr<ID3D11PixelShader> m_psGrayscale;
    winrt::com_ptr<ID3D11SamplerState> m_samplerState;
    winrt::com_ptr<ID3D11ShaderResourceView> m_videoSRV;
    winrt::com_ptr<ID3D11RenderTargetView> m_renderTargetView;
    std::atomic<VideoEffect> m_currentEffect{ VideoEffect::Normal };
    std::atomic<AudioEffect> m_currentAudioEffect{ AudioEffect::Normal };

    void StartPlayback();
    void InitializeShaders();
    void InitThumbnailDecoder();
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
    void ExportClip(std::chrono::duration<double> startTime, std::chrono::duration<double> endTime);
};