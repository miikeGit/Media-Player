#pragma once

#include "mfmediaengine.h"

struct MediaEngineNotify : winrt::implements<MediaEngineNotify, IMFMediaEngineNotify> {
    HRESULT EventNotify(
        _In_  DWORD event,
        _In_  DWORD_PTR param1,
        _In_  DWORD param2) noexcept override 
    {
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

private:
    winrt::com_ptr<ID3D11Device> m_d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> m_d3dDeviceContext;
    winrt::com_ptr<IDXGISwapChain1> m_swapChain;
    winrt::com_ptr<ID3D11Texture2D> m_backBuffer;
    winrt::com_ptr<IMFMediaEngine> m_mediaEngine;
    winrt::com_ptr<IMFDXGIDeviceManager> m_dxgiManager;
    UINT m_resetToken = 0;
    bool m_mfStarted = false;

    void InitializeDirectX();
    void InitializeMediaEngine();
};