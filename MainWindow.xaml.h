#pragma once

#include "MainWindow.g.h"
#include <chrono>

namespace winrt::MediaPlayer::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {

        MainWindow()
        {
            this->ExtendsContentIntoTitleBar(true);
            this->SetTitleBar(AppTitleBar());

            timer = winrt::Microsoft::UI::Xaml::DispatcherTimer();
			timer.Interval(std::chrono::milliseconds(100 / 60));
            timer.Tick({this, &MainWindow::OnTimerTick });
			timer.Start();
        }

        int32_t MyProperty();
        void MyProperty(int32_t value);

    private:
		winrt::Microsoft::UI::Xaml::DispatcherTimer timer{ nullptr };

        void OnTimerTick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Windows::Foundation::IInspectable const& e);

		winrt::com_ptr<ID3D11Device> D3DDevice;
        winrt::com_ptr<ID3D11DeviceContext> D3DDeviceContext;
		winrt::com_ptr<IDXGIDevice> DXGIDevice;

        void InitializeDirectX();

        winrt::com_ptr<IDXGISwapChain> swapChain;

		void InitializeSwapChain();
    };
}

namespace winrt::MediaPlayer::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
