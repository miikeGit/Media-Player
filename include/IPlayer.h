#pragma once
#include <chrono>

namespace winrt::Microsoft::UI::Xaml::Controls {
    struct SwapChainPanel;
}

struct SubItem {
    std::chrono::duration<double> startTime;
    std::chrono::duration<double> endTime;
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
    void LoadExternalSubtitles(std::vector<SubItem> subtitles);

    virtual void OpenAndPlay(const winrt::hstring& path) = 0;
    virtual void RenderFrame() = 0;
    virtual void Resize(UINT width, UINT height) = 0;
    virtual void Play() = 0;
    virtual void Pause() = 0;
    virtual void Stop() = 0;
    virtual void SetVolume(double volume) = 0;
    virtual void SetPlaybackSpeed(double speed) = 0;

    virtual std::chrono::duration<double> GetCurrentTime() const = 0;
    virtual std::chrono::duration<double> GetDuration() const = 0;

    virtual void SetCurrentTime(std::chrono::duration<double> time) = 0;

    virtual std::wstring GetCurrentSubtitle(std::chrono::duration<double> currentTime) = 0;
    virtual bool TakeScreenshot() = 0;

    virtual void StartClipRecording() = 0;
    virtual bool StopClipRecording() = 0;
    virtual bool IsClipRecording() const = 0;

    void SetEventCallback(std::function<void(DWORD, DWORD_PTR, DWORD)> callback);

protected:
    winrt::com_ptr<ID3D11Device> m_d3dDevice;
    winrt::com_ptr<ID3D11DeviceContext> m_d3dDeviceContext;
    winrt::com_ptr<IDXGISwapChain1> m_swapChain;
    winrt::com_ptr<ID3D11Texture2D> m_backBuffer;

    std::vector<SubItem> m_subtitles;
    std::mutex m_subtitleMutex;

    std::function<void(DWORD, DWORD_PTR, DWORD)> m_eventCallback;

    void InitializeDirectX();
    void FireEvent(DWORD event, DWORD_PTR param1 = 0, DWORD param2 = 0);
};