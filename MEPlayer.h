#pragma once

#include "mfmediaengine.h"
#include "IPlayer.h"
#include <mutex>

struct MediaEngineNotify : winrt::implements<MediaEngineNotify, IMFMediaEngineNotify> {
    std::function<void(DWORD, DWORD_PTR, DWORD)> OnEvent;

    HRESULT EventNotify(
        DWORD event,
        DWORD_PTR param1,
        DWORD param2) noexcept override
    {
        if (OnEvent) OnEvent(event, param1, param2);
        return S_OK;
    }
};

class MEPlayer : public IPlayer {
public:
    MEPlayer();
    ~MEPlayer();

    MEPlayer(const MEPlayer&) = delete;
    MEPlayer& operator=(const MEPlayer&) = delete;

    void OpenAndPlay(const winrt::hstring& path) override;
    void RenderFrame() override;
    void Resize(UINT width, UINT height) override;
    void Play() override;
    void Pause() override;
    void Stop() override;
    void SetVolume(double volume) override;

    double GetCurrentTime() const override;
    double GetDuration() const override;

    void SetCurrentTime(double time) override;
    void SetPlaybackSpeed(double speed) override;

    std::wstring GetCurrentSubtitle(double currentTime) override;
    void LoadExternalSubtitles(std::vector<SubItem> subtitles) override;

private:
    winrt::com_ptr<IMFMediaEngine> m_mediaEngine;
    winrt::com_ptr<IMFDXGIDeviceManager> m_dxgiManager;
    winrt::com_ptr<MediaEngineNotify> m_notify;
    UINT m_resetToken = 0;

    std::vector<SubItem> m_subtitles;
    std::mutex m_subtitleMutex;

    void InitializeMediaEngine();
};