#pragma once

#include "MainWindow.g.h"

namespace winrt::MediaPlayer::implementation {
    struct MainWindow : MainWindowT<MainWindow> {
        MainWindow() {
            //TODO: refactor
            InitializeComponent();

            this->ExtendsContentIntoTitleBar(true);
            this->SetTitleBar(AppTitleBar());

            timer = winrt::Microsoft::UI::Xaml::DispatcherTimer();
			timer.Interval(std::chrono::milliseconds(100 / 60));
            timer.Tick({this, &MainWindow::OnTimerTick });
			timer.Start();

			InitializeDirectX();
			InitializeSwapChain();
        }

        void MyButton_Click(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e) {
            MFStartup(MF_VERSION);
            MFCreateMediaSession(nullptr, mfmSession.put());
            winrt::com_ptr<IMFMediaSource> source;
            CreateMediaSource(L"path", source);
        }

    private:
		winrt::Microsoft::UI::Xaml::DispatcherTimer timer{ nullptr };

        void OnTimerTick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Windows::Foundation::IInspectable const& e);

		com_ptr<ID3D11Device> d3dDevice;
        com_ptr<ID3D11DeviceContext> d3dDeviceContext;
		com_ptr<IDXGIDevice> dxgiDevice;
        com_ptr<IDXGISwapChain1> swapChain;
		com_ptr<ID3D11Texture2D> backBuffer;
		com_ptr<ID3D11RenderTargetView> renderTargetView;
        com_ptr<IMFMediaSession> mfmSession;
        
        void InitializeDirectX();
        void InitializeSwapChain();
        void CreateMediaSource(LPCWSTR url, com_ptr<IMFMediaSource>& ppSource);
    };
}

namespace winrt::MediaPlayer::factory_implementation{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
