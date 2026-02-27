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

void MEPlayer::InitializeMediaEngine() {
    check_hresult(MFCreateDXGIDeviceManager(&m_resetToken, m_dxgiManager.put()));
    check_hresult(m_dxgiManager->ResetDevice(m_d3dDevice.get(), m_resetToken));

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
    m_notify->OnEvent = [this](DWORD event, DWORD_PTR param1, DWORD param2) {
        FireEvent(event, param1, param2);
    };
    check_hresult(attr->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, m_notify.get()));
    check_hresult(attr->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM));
    check_hresult(attr->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, m_dxgiManager.get()));

    check_hresult(factory.get()->CreateInstance(
        0,
        attr.get(),
        m_mediaEngine.put())
    );
}

void MEPlayer::OpenAndPlay(const hstring& path) {
	if (!m_mediaEngine) return;

    BSTR bstrPath = SysAllocString(path.c_str());
    check_hresult(m_mediaEngine->SetSource(bstrPath));
    SysFreeString(bstrPath);
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

void MEPlayer::Resize(UINT width, UINT height) {
    if (!m_swapChain) return;

    m_backBuffer = nullptr;
    check_hresult(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0));
    check_hresult(m_swapChain->GetBuffer(0, IID_PPV_ARGS(m_backBuffer.put())));
}

void MEPlayer::Play() {
    if (m_mediaEngine) m_mediaEngine->Play();
}

void MEPlayer::Pause() {
    if (m_mediaEngine) m_mediaEngine->Pause();
}

void MEPlayer::SetVolume(double volume) {
    if (m_mediaEngine) m_mediaEngine->SetVolume(volume / 100.0);
}

double MEPlayer::GetCurrentTime() const {
    if (!m_mediaEngine) return 0.0;
    return m_mediaEngine->GetCurrentTime();
}

double MEPlayer::GetDuration() const {
    if (!m_mediaEngine) return 0.0;
    double duration = m_mediaEngine->GetDuration();
    if (std::isnan(duration) || std::isinf(duration)) return 0.0;
    return duration;
}

void MEPlayer::SetCurrentTime(double time) {
    if (!m_mediaEngine) return;
    m_mediaEngine->SetCurrentTime(time);
}

void MEPlayer::Stop() {
	if (!m_mediaEngine) return;
    m_mediaEngine->Pause();
    ClearFrame();
    m_mediaEngine->SetSource(nullptr);
}

void MEPlayer::SetPlaybackSpeed(double speed) {
    if (!m_mediaEngine) return;
    m_mediaEngine->SetPlaybackRate(speed);
}