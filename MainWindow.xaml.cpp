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

        player = std::make_unique<MEPlayer>();
        player->SetSwapChainPanel(SwapChainCanvas());
        InitializeTimer();
    }

    MainWindow::~MainWindow() {
        if (timer) timer.Stop();
    }

    void MainWindow::InitializeTimer() {
        timer = DispatcherTimer();
        timer.Interval(std::chrono::milliseconds(16));
        timer.Tick({ this, &MainWindow::OnTimerTick });
        timer.Start();
    }

    void MainWindow::OnTimerTick(IInspectable const&, IInspectable const&) {
        player->RenderFrame();
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

            player->OpenAndPlay(bstrPath);

            SysFreeString(bstrPath);
        }
    }
    
    void MainWindow::SwapChainCanvasSizeChanged(IInspectable const&, SizeChangedEventArgs const& e) {
        player->Resize(
            static_cast<UINT>(e.NewSize().Width),
            static_cast<UINT>(e.NewSize().Height));
    }
}
