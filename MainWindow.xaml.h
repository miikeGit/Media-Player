#pragma once

#include "MainWindow.g.h"
#include <chrono>

namespace winrt::MediaPlayer::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {

        MainWindow()
        {
            // Xaml objects should not call InitializeComponent during construction.
            // See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent

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

		ID3D11Device* device{ nullptr };
		ID3D11DeviceContext* context{ nullptr };
        void InitializeDirectX();
    };
}

namespace winrt::MediaPlayer::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
