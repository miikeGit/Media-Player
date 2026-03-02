#pragma once

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <mfmediaengine.h>
#include <mfapi.h>
#include <functional>

struct SubItem {
    double startTime;
    double endTime;
    std::wstring text;
};

class IPlayer {
public:
    IPlayer() = default;
    virtual ~IPlayer() = default;

    IPlayer(const IPlayer&) = delete;
    IPlayer& operator=(const IPlayer&) = delete;

    void SetSwapChainPanel(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel);
    void ClearFrame();

    virtual void OpenAndPlay(const winrt::hstring& path) = 0;
    virtual void RenderFrame() = 0;
    virtual void Resize(UINT width, UINT height) = 0;
    virtual void Play() = 0;
    virtual void Pause() = 0;
    virtual void Stop() = 0;
    virtual void SetVolume(double volume) = 0;
    virtual void SetPlaybackSpeed(double speed) = 0;

    virtual double GetCurrentTime() const = 0;
    virtual double GetDuration() const = 0;

    virtual void SetCurrentTime(double time) = 0;

    virtual std::wstring GetCurrentSubtitle(double currentTime) = 0;
    virtual void LoadExternalSubtitles(std::vector<SubItem> subtitles) = 0;

    void SetEventCallback(std::function<void(DWORD, DWORD_PTR, DWORD)> callback);

protected:
    winrt::com_ptr<ID3D11Device> m_d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> m_d3dDeviceContext;
    winrt::com_ptr<IDXGISwapChain1> m_swapChain;
    winrt::com_ptr<ID3D11Texture2D> m_backBuffer;

    std::function<void(DWORD, DWORD_PTR, DWORD)> m_eventCallback;

    void InitializeDirectX();
    void FireEvent(DWORD event, DWORD_PTR param1 = 0, DWORD param2 = 0);
};