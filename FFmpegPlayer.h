#pragma once

#include "IPlayer.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

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
    void SetVolume(double volume) override {}

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

    double m_currentTime{ 0.0 };
    double m_duration = 0.0;

    std::thread m_decodeThread;
    std::mutex m_frameMutex;

    void DecodeThreadFunc();
    void CleanupFFmpeg();
};