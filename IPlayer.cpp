#include "pch.h"

#include "IPlayer.h"

#include <microsoft.ui.xaml.media.dxinterop.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml::Controls;

void IPlayer::InitializeDirectX() {
    check_hresult(D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION,
        m_d3dDevice.put(), nullptr, m_d3dDeviceContext.put()));

    com_ptr<ID3D11Multithread> multithread;
    m_d3dDevice.as(multithread);
    multithread->SetMultithreadProtected(TRUE);
}

void IPlayer::SetSwapChainPanel(SwapChainPanel const& panel) {
    auto panelNative = panel.as<ISwapChainPanelNative>();

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = 1;
    desc.Height = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SampleDesc.Count = 1;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    auto dxgiDevice = m_d3dDevice.as<IDXGIDevice>();
    com_ptr<IDXGIAdapter> adapter;
    check_hresult(dxgiDevice->GetAdapter(adapter.put()));

    com_ptr<IDXGIFactory2> factory;
    check_hresult(adapter->GetParent(IID_PPV_ARGS(factory.put())));
    check_hresult(factory->CreateSwapChainForComposition(dxgiDevice.get(), &desc, nullptr, m_swapChain.put()));
    check_hresult(panelNative->SetSwapChain(m_swapChain.get()));
    check_hresult(m_swapChain->GetBuffer(0, IID_PPV_ARGS(m_backBuffer.put())));
}

void IPlayer::ClearFrame() {
    if (!m_swapChain || !m_d3dDeviceContext || !m_backBuffer) return;

    com_ptr<ID3D11RenderTargetView> rtv;
    check_hresult(m_d3dDevice->CreateRenderTargetView(m_backBuffer.get(), nullptr, rtv.put()));

    const float black[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_d3dDeviceContext->ClearRenderTargetView(rtv.get(), black);
    m_swapChain->Present(0, 0);

    m_backBuffer = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(m_backBuffer.put()));
}

void IPlayer::SetEventCallback(std::function<void(DWORD, DWORD_PTR, DWORD)> callback) {
    m_eventCallback = std::move(callback);
}

void IPlayer::FireEvent(DWORD event, DWORD_PTR param1, DWORD param2) {
    if (m_eventCallback) {
        m_eventCallback(event, param1, param2);
    }
}

void IPlayer::LoadExternalSubtitles(std::vector<SubItem> subtitles) {
    std::lock_guard<std::mutex> lock(m_subtitleMutex);
    m_subtitles = std::move(subtitles);
}