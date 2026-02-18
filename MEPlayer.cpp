#include "pch.h"
#include "MEPlayer.h"

using namespace winrt;

MEPlayer::MEPlayer() {
    MFStartup(MF_VERSION);
    InitializeDirectX();
    InitializeMediaEngine();
}

MEPlayer::~MEPlayer() {
    if (m_mediaEngine) {
        m_mediaEngine->Shutdown();
        m_mediaEngine = nullptr;
    }
    MFShutdown();
}

void MEPlayer::InitializeDirectX() {
    check_hresult(D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        m_d3dDevice.put(),
        nullptr,
        m_d3dDeviceContext.put())
    );

    com_ptr<ID3D11Multithread> multithread;
    m_d3dDevice.as(multithread);
    multithread->SetMultithreadProtected(TRUE);

    check_hresult(MFCreateDXGIDeviceManager(&m_resetToken, m_dxgiManager.put()));
    check_hresult(m_dxgiManager->ResetDevice(m_d3dDevice.get(), m_resetToken));
}

void MEPlayer::InitializeMediaEngine() {
    com_ptr<IMFMediaEngineClassFactory> factory;

    check_hresult(CoCreateInstance(
        CLSID_MFMediaEngineClassFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(factory.put()))
    );

    com_ptr<IMFAttributes> attr;
    check_hresult(MFCreateAttributes(attr.put(), 1));

    m_notify = make_self<MediaEngineNotify>();
    check_hresult(attr->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, m_notify.get()));
    check_hresult(attr->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM));
    check_hresult(attr->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, m_dxgiManager.get()));

    check_hresult(factory.get()->CreateInstance(
        0,
        attr.get(),
        m_mediaEngine.put())
    );
}

void MEPlayer::SetEventCallback(std::function<void(DWORD, DWORD_PTR, DWORD)> callback) {
    m_notify->OnEvent = std::move(callback);
}

void MEPlayer::SetSwapChainPanel(Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) {
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

void MEPlayer::OpenAndPlay(BSTR path) {
    check_hresult(m_mediaEngine->SetSource(path));
    check_hresult(m_mediaEngine->Play());
}

void MEPlayer::RenderFrame() {
    if (!m_mediaEngine || !m_swapChain) return;

    LONGLONG pts;
    if (m_mediaEngine->OnVideoStreamTick(&pts) == S_OK) {
        DXGI_SWAP_CHAIN_DESC1 desc;
        m_swapChain->GetDesc1(&desc);
        RECT dstRect = { 0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height) };

        m_mediaEngine->TransferVideoFrame(m_backBuffer.get(), nullptr, &dstRect, nullptr);
        if (SUCCEEDED(m_swapChain->Present(0, 0))) {
            m_backBuffer = nullptr;
            check_hresult(m_swapChain->GetBuffer(0, IID_PPV_ARGS(m_backBuffer.put())));
        }
    }
}

void MEPlayer::ClearFrame() {
    if (!m_swapChain || !m_d3dDeviceContext || !m_backBuffer) return;

    winrt::com_ptr<ID3D11RenderTargetView> rtv;
    check_hresult(m_d3dDevice->CreateRenderTargetView(m_backBuffer.get(), nullptr, rtv.put()));

    const float black[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_d3dDeviceContext->ClearRenderTargetView(rtv.get(), black);
    m_swapChain->Present(0, 0);

    m_backBuffer = nullptr;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(m_backBuffer.put()));
}

void MEPlayer::Resize(UINT width, UINT height) {
    if (!m_swapChain) return;

    m_backBuffer = nullptr;
    check_hresult(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0));
    check_hresult(m_swapChain->GetBuffer(0, IID_PPV_ARGS(m_backBuffer.put())));
}

void MEPlayer::Play() {
    m_mediaEngine->Play();
}

void MEPlayer::Pause() {
    m_mediaEngine->Pause();
}

void MEPlayer::SetVolume(double volume) {
	m_mediaEngine->SetVolume(volume / 100.0);
}

double MEPlayer::GetCurrentTime() {
    if (!m_mediaEngine) return 0.0;
    return m_mediaEngine->GetCurrentTime();
}

double MEPlayer::GetDuration() {
    if (!m_mediaEngine) return 0.0;
    double duration = m_mediaEngine->GetDuration();
    if (std::isnan(duration) || std::isinf(duration)) return 0.0;
    return duration;
}

void MEPlayer::SetCurrentTime(double time) {
    if (!m_mediaEngine) return;
    m_mediaEngine->SetCurrentTime(time);
}