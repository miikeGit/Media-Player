#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::MediaPlayer::implementation
{
    MainWindow::MainWindow() {
        InitializeComponent();

        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());

        m_player = std::make_unique<MEPlayer>();
        m_player->SetSwapChainPanel(SwapChainCanvas());

        m_player->SetEventCallback([this](DWORD event, DWORD_PTR param1, DWORD) {
            this->DispatcherQueue().TryEnqueue([this, event, param1]() {
                switch (event) {
                case MF_MEDIA_ENGINE_EVENT_ERROR:
                    OutputDebugString(L"Media Engine Error\n");
                    break;
                case MF_MEDIA_ENGINE_EVENT_ENDED:
                    PlayPauseIcon().Symbol(Controls::Symbol::Play);
                    m_player->ClearFrame();
                    if (m_currentIndex + 1 < static_cast<int>(m_playlist.size()))
                        PlayAtIndex(m_currentIndex + 1);
                    break;
                case MF_MEDIA_ENGINE_EVENT_PLAYING:
                    PlayPauseIcon().Symbol(Controls::Symbol::Pause);
                    break;
                case MF_MEDIA_ENGINE_EVENT_PAUSE:
                    PlayPauseIcon().Symbol(Controls::Symbol::Play);
                    break;
                case MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA:
                    MediaTitle().Text(m_playlistItems.GetAt(m_currentIndex));
                    break;
                }
                });
            });

        TimeSlider().AddHandler(
            UIElement::PointerPressedEvent(),
            winrt::box_value(Input::PointerEventHandler{ this, &MainWindow::TimeSlider_PointerPressed }),
            true);
        TimeSlider().AddHandler(
            UIElement::PointerReleasedEvent(),
            winrt::box_value(Input::PointerEventHandler{ this, &MainWindow::TimeSlider_PointerReleased }),
            true);
        TimeSlider().AddHandler(
            UIElement::PointerCaptureLostEvent(),
            winrt::box_value(Input::PointerEventHandler{ this, &MainWindow::TimeSlider_PointerCaptureLost }),
            true);

        InitializeTimer();

        m_playlistItems = winrt::single_threaded_observable_vector<winrt::hstring>();
        PlaylistView().ItemsSource(m_playlistItems);
    }

    MainWindow::~MainWindow() {
        if (timer) timer.Stop();
    }

    void MainWindow::InitializeTimer() {
        auto queue = this->DispatcherQueue();
        timer = queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(16));
        timer.Tick({ this, &MainWindow::OnTimerTick });
        timer.Start();
    }

    hstring MainWindow::FormatTime(double seconds) {
        int totalSec = static_cast<int>(seconds);
        int hrs = totalSec / 3600;
        int mins = (totalSec % 3600) / 60;
        int secs = totalSec % 60;

        return hstring(std::format(L"{}:{:02}:{:02}", hrs, mins, secs));
    }

    void MainWindow::OnTimerTick(IInspectable const&, IInspectable const&) {
        if (!m_player) return;
        m_player->RenderFrame();

        double duration = m_player->GetDuration();
        double currentTime = m_player->GetCurrentTime();

        if (duration > 0.0) {
            DurationText().Text(FormatTime(duration));
            CurrentTimeText().Text(FormatTime(currentTime));

            if (!m_isSeeking) {
            TimeSlider().Maximum(duration);
            TimeSlider().Value(currentTime);
        }
    }
    }

    void MainWindow::TimeSlider_PointerPressed(IInspectable const&, Input::PointerRoutedEventArgs const&) {
        m_isSeeking = true;
    }

    void MainWindow::TimeSlider_PointerReleased(IInspectable const&, Input::PointerRoutedEventArgs const&) {
        if (m_isSeeking) {
            m_player->SetCurrentTime(TimeSlider().Value());
            m_isSeeking = false;
        }
    }

    void MainWindow::TimeSlider_PointerCaptureLost(IInspectable const&, Input::PointerRoutedEventArgs const&) {
        if (m_isSeeking) {
            m_player->SetCurrentTime(TimeSlider().Value());
            m_isSeeking = false;
        }
    }

    void MainWindow::OnOpenFileClick(IInspectable const&, RoutedEventArgs const&) {
        OpenFile(true);
    }

    void MainWindow::OnVolumeSliderValueChanged(
        Windows::Foundation::IInspectable const&,
        Controls::Primitives::RangeBaseValueChangedEventArgs const&) 
    {
		if (!m_player) return;

		VolumeText().Text(std::to_wstring(static_cast<int>(VolumeSlider().Value())));
		m_player->SetVolume(static_cast<double>(VolumeSlider().Value()));
    }

    Windows::Storage::Pickers::FileOpenPicker MainWindow::CreateFilePicker() {
        HWND hwnd;
        check_hresult(this->try_as<IWindowNative>()->get_WindowHandle(&hwnd));

        Windows::Storage::Pickers::FileOpenPicker picker{};
        picker.as<IInitializeWithWindow>()->Initialize(hwnd);
        picker.SuggestedStartLocation(Windows::Storage::Pickers::PickerLocationId::VideosLibrary);
        picker.FileTypeFilter().Append(L".mp4");
        picker.FileTypeFilter().Append(L".mkv");
        picker.FileTypeFilter().Append(L".avi");
        picker.FileTypeFilter().Append(L".mp3");
        picker.FileTypeFilter().Append(L".wav");
        picker.FileTypeFilter().Append(L".flac");
        picker.FileTypeFilter().Append(L".wma");
        picker.FileTypeFilter().Append(L".aac");
        return picker;
    }

    fire_and_forget MainWindow::OpenFile(bool playNow) {
        auto file = co_await CreateFilePicker().PickSingleFileAsync();

        if (file != nullptr) {
            m_playlist.push_back(file.Path());
            m_playlistItems.Append(std::filesystem::path(file.Path().c_str()).filename().wstring());
            if (m_playlist.size() == 1 || playNow) {
                PlayAtIndex(static_cast<int>(m_playlist.size() - 1));
            }
        }
    }
    
    void MainWindow::SwapChainCanvasSizeChanged(IInspectable const&, SizeChangedEventArgs const& e) {
        if (!m_player) return;

        m_player->Resize(
            static_cast<UINT>(e.NewSize().Width),
            static_cast<UINT>(e.NewSize().Height));
    }

    void MainWindow::OnVolumeUp(Input::KeyboardAccelerator const& sender, Input::KeyboardAcceleratorInvokedEventArgs const& args) {
        double current = VolumeSlider().Value();
        VolumeSlider().Value(current + 5);
    }

    void MainWindow::OnVolumeDown(Input::KeyboardAccelerator const& sender, Input::KeyboardAcceleratorInvokedEventArgs const& args) {
        double current = VolumeSlider().Value();
        VolumeSlider().Value(current - 5);
    }

    void MainWindow::OnPlayPauseKey(Input::KeyboardAccelerator const& sender, Input::KeyboardAcceleratorInvokedEventArgs const& args) {
        TogglePlayback();
	}

    void MainWindow::OnPlayPauseBtnClick(IInspectable const& sender, RoutedEventArgs const& e) {
        TogglePlayback();
	}

    void MainWindow::TogglePlayback() {
        if (PlayPauseIcon().Symbol() == Controls::Symbol::Play) {
            m_player->Play();
        }
        else {
            m_player->Pause();
        }
	}

    void MainWindow::OnTogglePlaylistClick(IInspectable const&, RoutedEventArgs const&) {
        PlaylistSplitView().OpenPaneLength(RootGrid().ActualWidth() / 2);
        PlaylistSplitView().IsPaneOpen(!PlaylistSplitView().IsPaneOpen());
    }

    void MainWindow::OnPlaylistSelectionChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&) {
        int idx = PlaylistView().SelectedIndex();
        if (idx != m_currentIndex)
            PlayAtIndex(idx);
    }

    void MainWindow::PlayAtIndex(int index) {
        if (index < 0 || index >= static_cast<int>(m_playlist.size())) return;
        m_currentIndex = index;
        PlaylistView().SelectedIndex(index);
        m_player->OpenAndPlay(m_playlist[index]);
    }

    void MainWindow::OnAddToPlaylistClick(IInspectable const&, RoutedEventArgs const&) {
        OpenFile(false);
    }

    void MainWindow::OnPreviousBtnClick(IInspectable const& sender, RoutedEventArgs const& e) {
        PlayAtIndex(m_currentIndex - 1);
    }

    void MainWindow::OnNextBtnClick(IInspectable const& sender, RoutedEventArgs const& e) {
        PlayAtIndex(m_currentIndex + 1);
    }

    void MainWindow::OnClearPlaylistClick(IInspectable const& sender, RoutedEventArgs const& e) {
        m_playlist.clear();
        m_playlistItems.Clear();
        m_currentIndex = -1;
        m_player->Stop();

        DurationText().Text(FormatTime(0.0));
        CurrentTimeText().Text(FormatTime(0.0));
        TimeSlider().Maximum(0.0);
        TimeSlider().Value(0.0);
        MediaTitle().Text(hstring{});
    }
}