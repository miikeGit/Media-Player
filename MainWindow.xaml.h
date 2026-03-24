#pragma once

#include "MainWindow.g.h"
#include "TorrentClient.h"

class FFmpegPlayer;
class MEPlayer;
class IPlayer;

namespace winrt::Windows::Storage::Pickers {
    struct FileOpenPicker;
}

namespace winrt::MediaPlayer::implementation {
    struct MainWindow : MainWindowT<MainWindow> {
        MainWindow();
        ~MainWindow();

        winrt::fire_and_forget OnOpenFileClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e,
            bool playNow = true);
        winrt::fire_and_forget OnOpenUrlClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void SwapChainCanvasSizeChanged(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& e);
        void OnPlayPauseKey(
            winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender,
            winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args);
        void OnPlayPauseBtnClick(
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
        void OnPreviousBtnClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnNextBtnClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnClearPlaylistClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnRemoveFromPlaylistClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnTempoItemClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        fire_and_forget OnOpenSubtitleFile(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnSaveScreenshotClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnClipRecordClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnSliderPointerEntered(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnSliderPointerExited(
            winrt::Windows::Foundation::IInspectable const& sender, 
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnSliderPointerMoved(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e);
        void OnTogglePipClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnEffectItemClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnAudioEffectItemClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void OnExitClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        fire_and_forget OnPlayFromMagnetClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        fire_and_forget OnPlayFromTrackerClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
        fire_and_forget OnPlayFromZipClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    private:
        std::atomic<double> m_requestedThumbnailTime = -1.0;
        std::atomic<bool> m_isThumbnailWorkerRunning = false;

        winrt::Windows::Foundation::IAsyncAction RunThumbnailWorkerAsync();
        std::unique_ptr<MEPlayer> m_mePlayer;
        std::unique_ptr<FFmpegPlayer> m_ffmpegPlayer;
        std::unique_ptr<TorrentClient> m_torrentClient;
        IPlayer* m_player = nullptr;
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

        static winrt::hstring FormatTime(double seconds);
        void TogglePlayback();
        void PlayAtIndex(int index);
        Windows::Storage::Pickers::FileOpenPicker CreateFilePicker(const std::vector<std::wstring>& extensions);
        void OnPlayerEvent(DWORD event, DWORD_PTR param1);
        std::string ExecCMD(std::wstring command);
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetToken();
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetOneDriveUrl(winrt::hstring sharedUrl);
    };
}

namespace winrt::MediaPlayer::factory_implementation{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}