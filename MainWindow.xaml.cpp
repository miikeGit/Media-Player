#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "winrt/Microsoft.UI.Xaml.Controls.h"
#include <format>

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
                    break;
                case MF_MEDIA_ENGINE_EVENT_PLAYING:
                    PlayPauseIcon().Symbol(Controls::Symbol::Pause);
                    break;
                case MF_MEDIA_ENGINE_EVENT_PAUSE:
                    PlayPauseIcon().Symbol(Controls::Symbol::Play);
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
        OpenFile();
    }

    fire_and_forget MainWindow::OpenFile() {
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

        Windows::Storage::StorageFile file = co_await picker.PickSingleFileAsync();

        if (file != nullptr) {
            m_player->ClearFrame();
            BSTR bstrPath = SysAllocString(file.Path().c_str());
            m_player->OpenAndPlay(bstrPath);
            SysFreeString(bstrPath);
        }
    }
    
    void MainWindow::SwapChainCanvasSizeChanged(IInspectable const&, SizeChangedEventArgs const& e) {
        if (!m_player) return;

        m_player->Resize(
            static_cast<UINT>(e.NewSize().Width),
            static_cast<UINT>(e.NewSize().Height));
    }

    void MainWindow::OnPlayPauseClick(IInspectable const& sender, RoutedEventArgs const& e) {
        if (PlayPauseIcon().Symbol() == Controls::Symbol::Play) {
            m_player->Play();
        }
        else {
            m_player->Pause();
        }
    }
}