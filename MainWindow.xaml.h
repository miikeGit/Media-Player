#pragma once

#include "MainWindow.g.h"
#include "mfmediaengine.h"
#include "MEPlayer.h"
#include <shobjidl.h>
#include "winrt/Windows.Storage.Pickers.h"
#include <microsoft.ui.xaml.window.h>

namespace winrt::MediaPlayer::implementation {
    struct MainWindow : MainWindowT<MainWindow> {
        MainWindow();
        ~MainWindow();

        void OnOpenFileClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

        void SwapChainCanvasSizeChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& e);

        void OnPlayPauseClick(winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    private:
        std::unique_ptr<MEPlayer> m_player;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer{ nullptr };

        void InitializeTimer();
        void OnTimerTick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Windows::Foundation::IInspectable const& e);

        fire_and_forget OpenFile();
    };
}

namespace winrt::MediaPlayer::factory_implementation{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
