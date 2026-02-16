#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::MediaPlayer::implementation
{
    void MainWindow::OnTimerTick(IInspectable const&, IInspectable const&) {
        if (!renderTargetView) return;
        // test
        const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
        d3dDeviceContext->ClearRenderTargetView(renderTargetView.get(), clearColor);
        //vsync true
        swapChain->Present(1, 0);
    }

    void MainWindow::InitializeDirectX() {
        check_hresult(D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            d3dDevice.put(),
            nullptr,
            d3dDeviceContext.put())
        );

        check_hresult(MFCreateDXGIDeviceManager(&resetToken, dxgiManager.put()));
        check_hresult(dxgiManager->ResetDevice(d3dDevice.get(), resetToken));
    }

    void MainWindow::InitializeSwapChain() {
        auto panelNative = this->SwapChainCanvas().as<ISwapChainPanelNative>();

        uint32_t width = static_cast<uint32_t>(this->SwapChainCanvas().ActualWidth());
        uint32_t height = static_cast<uint32_t>(this->SwapChainCanvas().ActualHeight());

        if (width == 0) width = this->SwapChainCanvas().Width();
        if (height == 0) height = this->SwapChainCanvas().Height();

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.Width = width;
        swapChainDesc.Height = height;
        swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
		swapChainDesc.Flags = 0;
		swapChainDesc.Stereo = false;   

        auto dxgiDevice = d3dDevice.as<IDXGIDevice>();
		com_ptr<IDXGIAdapter> dxgiAdapter;
        check_hresult(dxgiDevice->GetAdapter(dxgiAdapter.put()));
		
        com_ptr<IDXGIFactory2> dxgiFactory2;
        check_hresult(
            dxgiAdapter->GetParent(IID_PPV_ARGS(dxgiFactory2.put()))
        );

		check_hresult(
            dxgiFactory2->CreateSwapChainForComposition(
                dxgiDevice.get(), &swapChainDesc, nullptr, swapChain.put()
            )
        );

        check_hresult(panelNative->SetSwapChain(swapChain.get()));
        check_hresult(swapChain.get()->GetBuffer(0, IID_PPV_ARGS(backBuffer.put())));
        check_hresult(
            d3dDevice->CreateRenderTargetView(
                backBuffer.get(), nullptr, renderTargetView.put()
            )
        );
	}

    void MainWindow::InitializeMediaEngine() {
        com_ptr<IMFMediaEngineClassFactory> factory;

        check_hresult(CoCreateInstance(
            CLSID_MFMediaEngineClassFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.put()))
        );

        com_ptr<IMFAttributes> attr;
        check_hresult(MFCreateAttributes(attr.put(), 1));
        
        auto notify = make<MediaEngineNotify>();
        check_hresult(attr->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify.get()));
        check_hresult(attr->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM));
        check_hresult(attr->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, dxgiManager.get()));

        check_hresult(factory.get()->CreateInstance(
            0,
            attr.get(),
            mediaEngine.put())
        );
    }

    void MainWindow::InitializeTimer() {
        timer = DispatcherTimer();
        timer.Interval(std::chrono::milliseconds(16));
        timer.Tick({ this, &MainWindow::OnTimerTick });
        timer.Start();
    }

    void MainWindow::onOpenFileClick(
        Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::RoutedEventArgs const& e) {
        openFile();
    }

    fire_and_forget MainWindow::openFile() {
        HWND hwnd;
        this->try_as<IWindowNative>()->get_WindowHandle(&hwnd);
        
        Windows::Storage::Pickers::FileOpenPicker picker{};
        picker.as<IInitializeWithWindow>()->Initialize(hwnd);
        picker.SuggestedStartLocation(Windows::Storage::Pickers::PickerLocationId::VideosLibrary);

        picker.FileTypeFilter().Append(L".mp4");
        picker.FileTypeFilter().Append(L".mkv");
        picker.FileTypeFilter().Append(L".avi");

        Windows::Storage::StorageFile file = co_await picker.PickSingleFileAsync();

        if (file != nullptr) {
            file.DisplayName();
        }
    }
}
