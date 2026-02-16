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
        winrt::check_hresult(D3D11CreateDevice(
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

        dxgiDevice = d3dDevice.as<IDXGIDevice>();
    }

    void MainWindow::InitializeSwapChain() {

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
        swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
		swapChainDesc.Flags = 0;
		swapChainDesc.Stereo = FALSE;

		com_ptr<IDXGIAdapter> dxgiAdapter;
		dxgiDevice.get()->GetAdapter(dxgiAdapter.put());
		com_ptr<IDXGIFactory2> dxgiFactory2;
		winrt::check_hresult(dxgiAdapter.get()->GetParent(__uuidof(IDXGIFactory2), dxgiFactory2.put_void()));
		winrt::check_hresult(dxgiFactory2->CreateSwapChainForComposition(dxgiDevice.get(), &swapChainDesc, nullptr, swapChain.put()));

        auto panelNative = this->SwapChainCanvas().as<ISwapChainPanelNative>();
        winrt::check_hresult(panelNative->SetSwapChain(swapChain.get()));

        swapChain.get()->GetBuffer(0, __uuidof(backBuffer), backBuffer.put_void());
        d3dDevice->CreateRenderTargetView(backBuffer.get(), nullptr, renderTargetView.put());
	}

    void MainWindow::InitializeMediaEngine() {
        winrt::com_ptr<IMFMediaEngineClassFactory> factory;
        
        winrt::check_hresult(CoCreateInstance(
            IID_IMFMediaEngineClassFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.put()))
        );

        com_ptr<IMFAttributes> attr;
        winrt::check_hresult(MFCreateAttributes(attr.put(), 1));
        
        auto notify = winrt::make<MediaEngineNotify>();
        winrt::check_hresult(attr->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify.get()));
        
        winrt::check_hresult(attr->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, 21)); // D3DFMT_A8R8G8B8
        
        MFCreateDXGIDeviceManager(&resetToken, deviceManager.put());
        deviceManager->ResetDevice(dxgiDevice.get(), resetToken);
        winrt::check_hresult(attr->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, deviceManager.get()));

        winrt::check_hresult(factory.get()->CreateInstance(
            0,
            attr.get(),
            me.put())
        );
    }
}
