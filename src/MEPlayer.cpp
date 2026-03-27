#include "pch.h"

#include "MEPlayer.h"

#include <wincodec.h>
#include <directxtk/ScreenGrab.h>
#include <mfreadwrite.h>
#include <mfapi.h>
#include <winrt/Windows.Storage.h>

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage;

MEPlayer::MEPlayer() {
    MFStartup(MF_VERSION);
    InitializeDirectX();
    InitializeMediaEngine();
}

MEPlayer::~MEPlayer() {
    if (m_clipExportThread.joinable()) m_clipExportThread.join();
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
    m_currentMediaPath = path.c_str();

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

std::chrono::duration<double> MEPlayer::GetCurrentTime() const {
    if (!m_mediaEngine) return std::chrono::duration<double>(0);
    return std::chrono::duration<double>(m_mediaEngine->GetCurrentTime());
}

std::chrono::duration<double> MEPlayer::GetDuration() const {
    if (!m_mediaEngine) return std::chrono::duration<double>(0);

    auto duration = std::chrono::duration<double>(m_mediaEngine->GetDuration());
    
    if (std::isnan(duration.count()) || std::isinf(duration.count())) return std::chrono::duration<double>(0);
    
    return duration;
}

void MEPlayer::SetCurrentTime(std::chrono::duration<double> time) {
    if (!m_mediaEngine) return;
    m_mediaEngine->SetCurrentTime(time.count());
}

void MEPlayer::Stop() {
	if (!m_mediaEngine) return;
    m_isClipRecording = false;
    if (m_clipExportThread.joinable()) m_clipExportThread.join();
    m_mediaEngine->Pause();
    ClearFrame();
    m_mediaEngine->SetSource(nullptr);
}

void MEPlayer::SetPlaybackSpeed(double speed) {
    if (!m_mediaEngine) return;
    m_mediaEngine->SetPlaybackRate(speed);
}

std::wstring MEPlayer::GetCurrentSubtitle(std::chrono::duration<double> currentTime) {
    std::lock_guard<std::mutex> lock(m_subtitleMutex);
    for (const auto& sub : m_subtitles) {
        if (currentTime >= sub.startTime && currentTime <= sub.endTime)
            return sub.text;
    }
    return L"";
}

bool MEPlayer::TakeScreenshot() {
    if (!m_mediaEngine) return false;

    DWORD videoWidth = 0, videoHeight = 0;
    m_mediaEngine->GetNativeVideoSize(&videoWidth, &videoHeight);
    if (!videoHeight || !videoWidth) return false;

    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = videoWidth;
    texDesc.Height = videoHeight;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    com_ptr<ID3D11Texture2D> renderTexture;
    check_hresult(m_d3dDevice->CreateTexture2D(&texDesc, nullptr, renderTexture.put()));

    RECT dstRect = { 0, 0, static_cast<LONG>(videoWidth), static_cast<LONG>(videoHeight) };
    check_hresult(m_mediaEngine->TransferVideoFrame(renderTexture.get(), nullptr, &dstRect, nullptr));

    std::filesystem::path path = std::filesystem::path(KnownFolders::SavedPictures().Path().c_str()) / L"screenshot.png";
    check_hresult(DirectX::SaveWICTextureToFile(
        m_d3dDeviceContext.get(), renderTexture.get(), GUID_ContainerFormatPng, path.c_str())
    );

    return true;
}

void MEPlayer::StartClipRecording() {
    m_clipStartTime = GetCurrentTime().count();
    m_isClipRecording = true;
}

bool MEPlayer::StopClipRecording() {
    if (!m_isClipRecording.load()) return false;
    m_isClipRecording = false;

    double clipEnd = GetCurrentTime().count();
    if (clipEnd <= m_clipStartTime) return false;

    if (m_clipExportThread.joinable()) m_clipExportThread.join();
    m_clipExportThread = std::thread(&MEPlayer::ExportClip, this, m_clipStartTime, clipEnd);
    return true;
}

bool MEPlayer::IsClipRecording() const {
    return m_isClipRecording.load();
}

void MEPlayer::ExportClip(double startTime, double endTime) {
    PWSTR pathTmp = nullptr;
    std::wstring videoPath;
    SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &pathTmp);
    videoPath = pathTmp;
    CoTaskMemFree(pathTmp);

    std::filesystem::path path =
        std::filesystem::path(videoPath) / std::wstring(to_hstring(GuidHelper::CreateNewGuid()) + L".mp4");

    com_ptr<IMFSourceReader> reader;
    com_ptr<IMFSinkWriter> writer;

    if (FAILED(MFCreateSourceReaderFromURL(m_currentMediaPath.c_str(), nullptr, reader.put()))) return;
    if (FAILED(MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, writer.put()))) return;

    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);

    std::vector<DWORD> streamMap;
    DWORD sinkIndex = 0;

    for (DWORD i = 0; true; ++i) {
        com_ptr<IMFMediaType> nativeType;
        if (FAILED(reader->GetNativeMediaType(i, 0, nativeType.put()))) break;

        GUID majorType;
        nativeType->GetMajorType(&majorType);

        if (majorType != MFMediaType_Video && majorType != MFMediaType_Audio) {
            streamMap.push_back(0); // skip
            continue;
        }

        reader->SetCurrentMediaType(i, nullptr, nativeType.get());

        if (SUCCEEDED(writer->AddStream(nativeType.get(), &sinkIndex)) &&
            SUCCEEDED(writer->SetInputMediaType(sinkIndex, nativeType.get(), nullptr)))
        {
            reader->SetStreamSelection(i, TRUE);
            streamMap.push_back(sinkIndex);
        }
        else {
            streamMap.push_back(0); // skip
        }
    }

    PROPVARIANT seekPos;
    seekPos.vt = VT_I8; // LONGLONG
    seekPos.hVal.QuadPart = static_cast<LONGLONG>(startTime * 10000000); // 100 nanosec
    reader->SetCurrentPosition(GUID_NULL, seekPos);

    if (FAILED(writer->BeginWriting())) return;

    LONGLONG end = static_cast<LONGLONG>(endTime * 10000000);
    std::vector<LONGLONG> startDts(streamMap.size(), -1);

    while (true) {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        com_ptr<IMFSample> sample;

        if (FAILED(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_ANY_STREAM), 0, &streamIndex, &flags, &timestamp, sample.put())) ||
            (flags & MF_SOURCE_READERF_ENDOFSTREAM) ||
            (timestamp > end)) {
            break;
        }
        if (!sample) continue;

        if (startDts[streamIndex] == -1) startDts[streamIndex] = timestamp;
        LONGLONG newTime = timestamp - startDts[streamIndex];
        sample->SetSampleTime(newTime < 0 ? 0 : newTime);
        writer->WriteSample(streamMap[streamIndex], sample.get());
    }

    writer->Finalize();
}