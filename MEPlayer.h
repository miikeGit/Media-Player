#pragma once

#include "mfmediaengine.h"
#include <functional>

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

class MEPlayer {
public:
    MEPlayer();
    ~MEPlayer();

    MEPlayer(const MEPlayer&) = delete;
    MEPlayer& operator=(const MEPlayer&) = delete;

    void SetSwapChainPanel(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel);
    void OpenAndPlay(BSTR path);
    void RenderFrame();
    void Resize(UINT width, UINT height);
    void Play();
    void Pause();

    double GetCurrentTime();
    double GetDuration();
    void SetCurrentTime(double time);

    void SetEventCallback(std::function<void(DWORD, DWORD_PTR, DWORD)> callback);

private:
    winrt::com_ptr<ID3D11Device> m_d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> m_d3dDeviceContext;
    winrt::com_ptr<IDXGISwapChain1> m_swapChain;
    winrt::com_ptr<ID3D11Texture2D> m_backBuffer;
    winrt::com_ptr<IMFMediaEngine> m_mediaEngine;
    winrt::com_ptr<IMFDXGIDeviceManager> m_dxgiManager;
    winrt::com_ptr<MediaEngineNotify> m_notify;
    UINT m_resetToken = 0;
    bool m_mfStarted = false;

    void InitializeDirectX();
    void InitializeMediaEngine();
};