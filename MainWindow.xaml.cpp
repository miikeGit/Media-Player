#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "winrt/Microsoft.UI.Xaml.Controls.h"

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

    void MainWindow::OnTimerTick(IInspectable const&, IInspectable const&) {
        if (!m_player) return;
        m_player->RenderFrame();
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

        Windows::Storage::StorageFile file = co_await picker.PickSingleFileAsync();

        if (file != nullptr) {
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
        if (!m_player->HasVideo()) return;

        if (m_player->IsPaused()) {
            m_player->Play();
        }
        else {
            m_player->Pause();
        }
    }
}