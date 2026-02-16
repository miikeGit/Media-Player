#pragma once

#include "MainWindow.g.h"
#include "mfmediaengine.h"
#include "MEPlayer.h"

namespace winrt::MediaPlayer::implementation {
    struct MainWindow : MainWindowT<MainWindow> {
        MainWindow() {
            InitializeComponent();

            this->ExtendsContentIntoTitleBar(true);
            this->SetTitleBar(AppTitleBar());

            MFStartup(MF_VERSION);
			InitializeDirectX();
			InitializeSwapChain();
            InitializeMediaEngine();
            InitializeTimer();
        }

        void MyButton_Click(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e) {
            
        }

    private:
        winrt::Microsoft::UI::Xaml::DispatcherTimer timer{ nullptr };

        void OnTimerTick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Windows::Foundation::IInspectable const& e);

		com_ptr<ID3D11Device> d3dDevice;
        com_ptr<ID3D11DeviceContext> d3dDeviceContext;
        com_ptr<IDXGISwapChain1> swapChain;
		com_ptr<ID3D11Texture2D> backBuffer;
		com_ptr<ID3D11RenderTargetView> renderTargetView;
        com_ptr<IMFMediaEngine> me;
        com_ptr<IMFDXGIDeviceManager> dxgiManager;

        UINT resetToken;

        void InitializeDirectX();
        void InitializeSwapChain();
        void InitializeMediaEngine();
        void InitializeTimer();
    };
}

namespace winrt::MediaPlayer::factory_implementation{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
