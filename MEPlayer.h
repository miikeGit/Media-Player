#pragma once

#include "mfmediaengine.h"
#include "IPlayer.h"

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

    void OpenAndPlay(const winrt::hstring& path);
    void RenderFrame();
    void Resize(UINT width, UINT height);
    void Play();
    void Pause();
    void Stop();
	void SetVolume(double volume);

    double GetCurrentTime() const;
    double GetDuration() const;

    void SetCurrentTime(double time);

    void SetEventCallback(std::function<void(DWORD, DWORD_PTR, DWORD)> callback);

private:
    winrt::com_ptr<IMFMediaEngine> m_mediaEngine;
    winrt::com_ptr<IMFDXGIDeviceManager> m_dxgiManager;
    winrt::com_ptr<MediaEngineNotify> m_notify;
    UINT m_resetToken = 0;

    void InitializeMediaEngine();
};