#pragma once

#include "MainWindow.g.h"
#include "MEPlayer.h"
#include "pch.h"

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
        void OnPlayPauseKey(
            winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
            winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void OnPlayPauseBtn(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnVolumeSliderValueChanged(
            winrt::Windows::Foundation::IInspectable const& sender, 
            winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& e);
        void OnVolumeUp(
            winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
            winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void OnVolumeDown(
            winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
            winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void OnTogglePlaylistClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnPlaylistSelectionChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
        void OnAddToPlaylistClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    private:
        std::unique_ptr<MEPlayer> m_player;
        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer{ nullptr };
        bool m_isSeeking = false;

        std::vector<winrt::hstring> m_playlist;
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::hstring> m_playlistItems;
        int m_currentIndex = -1;

        void InitializeTimer();
        void OnTimerTick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Windows::Foundation::IInspectable const& e);
        void TimeSlider_PointerPressed(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void TimeSlider_PointerReleased(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void TimeSlider_PointerCaptureLost(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);

        fire_and_forget OpenFile(bool playNow);
        static winrt::hstring FormatTime(double seconds);
        void TogglePlayback();
        void PlayAtIndex(int index);
        Windows::Storage::Pickers::FileOpenPicker CreateFilePicker();
    };
}

namespace winrt::MediaPlayer::factory_implementation{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
