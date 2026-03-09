#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include "FFmpegPlayer.h"
#include "srtparser.h"
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Foundation.h>
#include <MemoryBuffer.h>
#include <winrt/Microsoft.UI.Input.h>
#include <wil/cppwinrt_helpers.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <ShObjIdl_core.h> 
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Foundation.Collections.h>

using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Microsoft::UI::Xaml::Media::Imaging;
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::MediaPlayer::implementation
{
    MainWindow::MainWindow() {
        InitializeComponent();

        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());

        //m_mePlayer = std::make_unique<MEPlayer>();
        //m_mePlayer->SetSwapChainPanel(SwapChainCanvas());

        m_ffmpegPlayer = std::make_unique<FFmpegPlayer>();
        m_ffmpegPlayer->SetSwapChainPanel(SwapChainCanvas());

        //m_player = m_mePlayer.get();
        m_player = m_ffmpegPlayer.get();
        m_player->SetEventCallback([this](DWORD event, DWORD_PTR param1, DWORD) {
            OnPlayerEvent(event, param1);
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

    void MainWindow::OnPlayerEvent(DWORD event, DWORD_PTR param1) {
        this->DispatcherQueue().TryEnqueue([this, event, param1]() {
            switch (event) {
            case MF_MEDIA_ENGINE_EVENT_ERROR:
                if (m_player == m_mePlayer.get() && param1 == MF_MEDIA_ENGINE_ERR_SRC_NOT_SUPPORTED) {
                    if (!m_ffmpegPlayer) {
                        m_ffmpegPlayer = std::make_unique<FFmpegPlayer>();
                        m_ffmpegPlayer->SetEventCallback([this](DWORD e, DWORD_PTR p1, DWORD) {
                            OnPlayerEvent(e, p1);
                            });
                    }
                    m_ffmpegPlayer->SetSwapChainPanel(SwapChainCanvas());
                    m_player = m_ffmpegPlayer.get();
                    m_player->SetVolume(VolumeSlider().Value());
                    auto w = static_cast<UINT>(SwapChainCanvas().ActualWidth());
                    auto h = static_cast<UINT>(SwapChainCanvas().ActualHeight());
                    if (w > 0 && h > 0) m_player->Resize(w, h);
                    if (!m_playlist.empty() && m_currentIndex >= 0)
                        m_player->OpenAndPlay(m_playlist[m_currentIndex]);
                }
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
                if (m_playlistItems.Size() != 0)
                    MediaTitle().Text(m_playlistItems.GetAt(m_currentIndex));
                break;
            }
            });
    }

    MainWindow::~MainWindow() {
        if (timer) timer.Stop();
    }

    void MainWindow::InitializeTimer() {
        auto queue = this->DispatcherQueue();
        timer = queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(15));
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

            std::wstring sub = m_player->GetCurrentSubtitle(currentTime);
            if (sub.empty()) {
                SubtitleBorder().Visibility(Visibility::Collapsed);
            }
            else {
                SubtitleText().Text(winrt::hstring{ sub });
                SubtitleBorder().Visibility(Visibility::Visible);
            }

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

    fire_and_forget MainWindow::OnOpenFileClick(IInspectable const&, RoutedEventArgs const&, bool playNow) {
        auto file = co_await CreateFilePicker({ L"*" }).PickSingleFileAsync();

        if (file != nullptr) {
            m_playlist.push_back(file.Path());
            m_playlistItems.Append(file.Name());
            if (m_playlist.size() == 1 || playNow) {
                PlayAtIndex(static_cast<int>(m_playlist.size() - 1));
            }
        }
    }

    std::string MainWindow::ExecCMD(std::wstring command) {
        HANDLE read, write;
        SECURITY_ATTRIBUTES sa {sizeof(sa), nullptr, true};

        if (!CreatePipe(&read, &write, &sa, 0)) return "";
        // overwrite read pipe inheritance to false, otherwise UB
        SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFO si {sizeof(si)};
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = write;

        PROCESS_INFORMATION pi{};
        if (CreateProcess(nullptr, command.data(), nullptr, nullptr, true, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(write);
            std::string result;
            char buffer[1024];
            DWORD bytesRead;
            while (ReadFile(read, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
                result.append(buffer, bytesRead);
            }
            CloseHandle(read);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return result;
        }
        return "";
    }

    winrt::fire_and_forget MainWindow::OnOpenUrlClick(IInspectable const&, RoutedEventArgs const&) {
        TextBox input;
        input.PlaceholderText(L"Paste YouTube/OneDrive link here");
        input.Width(450);

        ContentDialog dialog;
        dialog.Content(input);
        dialog.PrimaryButtonText(L"Play");
        dialog.CloseButtonText(L"Cancel");
        dialog.XamlRoot(Content().XamlRoot());

        auto result = co_await dialog.ShowAsync();

        if (result == ContentDialogResult::Primary) {
            std::wstring inputUrl = input.Text().c_str();
            std::string resultUrl = "";

            if (inputUrl.find(L"1drv.ms") != std::wstring::npos || inputUrl.find(L"onedrive") != std::wstring::npos) {
                winrt::hstring directLink = co_await GetOneDriveUrl(winrt::hstring(inputUrl));
                resultUrl = winrt::to_string(directLink);
            }
            else {
                co_await winrt::resume_background();
                resultUrl = ExecCMD(L"yt-dlp.exe --no-warnings -g " + inputUrl);
            }
            co_await wil::resume_foreground(DispatcherQueue());

            if (resultUrl.empty()) co_return;
            m_player->OpenAndPlay(winrt::to_hstring(resultUrl));
        }
    }

    void MainWindow::OnVolumeSliderValueChanged(
        Windows::Foundation::IInspectable const&,
        Controls::Primitives::RangeBaseValueChangedEventArgs const&)
    {
        if (!m_player) return;

        VolumeText().Text(std::to_wstring(static_cast<int>(VolumeSlider().Value())));
        m_player->SetVolume(static_cast<double>(VolumeSlider().Value()));
    }

    Windows::Storage::Pickers::FileOpenPicker MainWindow::CreateFilePicker(const std::vector<std::wstring>& extensions) {
        Windows::Storage::Pickers::FileOpenPicker picker{};
        picker.as<IInitializeWithWindow>()->Initialize(GetWindowFromWindowId(AppWindow().Id()));
        for (const auto& ex : extensions) {
            picker.FileTypeFilter().Append(ex);
        }
        return picker;
    }

    void MainWindow::SwapChainCanvasSizeChanged(IInspectable const&, SizeChangedEventArgs const& e) {
        if (!m_player) return;

        m_player->Resize(
            static_cast<UINT>(e.NewSize().Width),
            static_cast<UINT>(e.NewSize().Height));
    }

    void MainWindow::OnVolumeUp(Input::KeyboardAccelerator const&, Input::KeyboardAcceleratorInvokedEventArgs const&) {
        double current = VolumeSlider().Value();
        VolumeSlider().Value(current + 5);
    }

    void MainWindow::OnVolumeDown(Input::KeyboardAccelerator const&, Input::KeyboardAcceleratorInvokedEventArgs const&) {
        double current = VolumeSlider().Value();
        VolumeSlider().Value(current - 5);
    }

    void MainWindow::OnPlayPauseKey(Input::KeyboardAccelerator const&, Input::KeyboardAcceleratorInvokedEventArgs const&) {
        TogglePlayback();
    }

    void MainWindow::OnPlayPauseBtnClick(IInspectable const&, RoutedEventArgs const&) {
        TogglePlayback();
    }

    void MainWindow::TogglePlayback() {
        if (m_player->GetDuration() > 0) {
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

        //if (m_player != m_mePlayer.get()) {
        //    if (m_ffmpegPlayer) m_ffmpegPlayer->Stop();
        //    m_mePlayer->SetSwapChainPanel(SwapChainCanvas());
        //    auto w = static_cast<UINT>(SwapChainCanvas().ActualWidth());
        //    auto h = static_cast<UINT>(SwapChainCanvas().ActualHeight());
        //    if (w > 0 && h > 0) m_mePlayer->Resize(w, h);
        //    m_player = m_mePlayer.get();
        //    m_player->SetVolume(VolumeSlider().Value());
        //}

        m_player->OpenAndPlay(m_playlist[index]);
    }

    void MainWindow::OnAddToPlaylistClick(IInspectable const&, RoutedEventArgs const&) {
        OnOpenFileClick(nullptr, nullptr, false);
    }

    void MainWindow::OnPreviousBtnClick(IInspectable const&, RoutedEventArgs const&) {
        PlayAtIndex(m_currentIndex - 1);
    }

    void MainWindow::OnNextBtnClick(IInspectable const&, RoutedEventArgs const&) {
        PlayAtIndex(m_currentIndex + 1);
    }

    void MainWindow::OnClearPlaylistClick(IInspectable const&, RoutedEventArgs const&) {
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

    void MainWindow::OnRemoveFromPlaylistClick(IInspectable const& sender, RoutedEventArgs const&) {
        auto item = sender.as<Controls::Button>().DataContext();
        uint32_t index;
        if (!m_playlistItems.IndexOf(unbox_value<hstring>(item), index)) return;

        if (m_playlist.size() == 1) {
            OnClearPlaylistClick(nullptr, nullptr);
            return;
        }

        bool wasPlaying = (static_cast<int>(index) == m_currentIndex);

        if (static_cast<int>(index) < m_currentIndex)
            m_currentIndex--;

        m_playlist.erase(m_playlist.begin() + index);
        m_playlistItems.RemoveAt(index);

        if (wasPlaying) {
            PlayAtIndex(
                (std::min)
                (static_cast<int>(index), static_cast<int>(m_playlist.size()) - 1)
            );
        }
    }

    void MainWindow::OnTempoItemClick(IInspectable const& sender, RoutedEventArgs const&) {
        if (!m_player) return;

        auto item = sender.as<Controls::RadioMenuFlyoutItem>();
        double speed = std::wcstod(winrt::unbox_value<winrt::hstring>(item.Tag()).c_str(), nullptr);
        m_player->SetPlaybackSpeed(speed);
        TempoButton().Content(winrt::box_value(item.Text()));
    }

    fire_and_forget MainWindow::OnOpenSubtitleFile(IInspectable const&, RoutedEventArgs const&) {
        auto file = co_await CreateFilePicker({ L".srt" }).PickSingleFileAsync();
        if (!file) co_return;

        std::unique_ptr<SubtitleParser> parser(
            SubtitleParserFactory(winrt::to_string(file.Path())).getParser()
        );

        std::vector<SubItem> subtitles;
        for (SubtitleItem* item : parser->getSubtitles()) {
            if (item->getIgnoreStatus()) continue;
            subtitles.push_back({
                item->getStartTime() / 1000.0,
                item->getEndTime() / 1000.0,
                std::wstring(winrt::to_hstring(item->getDialogue()))
                });
        }

        if (m_player) {
            m_player->LoadExternalSubtitles(std::move(subtitles));
        }
    }

    void MainWindow::OnSaveScreenshotClick(IInspectable const&, RoutedEventArgs const&) {
        m_player->TakeScreenshot();
    }
    
    void MainWindow::OnClipRecordClick(IInspectable const&, RoutedEventArgs const&) {
        if (!m_player || !m_player->GetDuration()) return;

        auto icon = ClipRecordButton().Content().as<Controls::FontIcon>();

        if (m_player->IsClipRecording()) {
            m_player->StopClipRecording();
            Controls::ToolTipService::SetToolTip(ClipRecordButton(), winrt::box_value(L"Record clip"));
            icon.Glyph(L"\uEA3A");
            icon.Foreground(Microsoft::UI::Xaml::Media::SolidColorBrush(Microsoft::UI::Colors::White()));
        }
        else {
            m_player->StartClipRecording();
            Controls::ToolTipService::SetToolTip(ClipRecordButton(), winrt::box_value(L"Stop recording"));
            icon.Glyph(L"\uE71A");
            icon.Foreground(Microsoft::UI::Xaml::Media::SolidColorBrush(Microsoft::UI::Colors::Red()));
        }
    }

    void MainWindow::OnSliderPointerEntered(IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) {
        ThumbnailPopup().IsOpen(true);
    }

    void MainWindow::OnSliderPointerExited(IInspectable const&, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) {
        ThumbnailPopup().IsOpen(false);
    }

    void MainWindow::OnSliderPointerMoved(IInspectable const& sender, Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e) {
        auto slider = sender.as<Microsoft::UI::Xaml::Controls::Slider>();

        double ratio = e.GetCurrentPoint(slider).Position().X / slider.ActualWidth();
        double target = ratio * m_player->GetDuration();

        ThumbnailPopup().HorizontalOffset(e.GetCurrentPoint(slider).Position().X);

        // overwrite
        m_requestedThumbnailTime.store(target);

        if (!m_isThumbnailWorkerRunning.exchange(true)) {
            RunThumbnailWorkerAsync();
        }
    }

    Windows::Foundation::IAsyncAction MainWindow::RunThumbnailWorkerAsync() {
        auto queue = this->DispatcherQueue();

        while (true) {
            double time = m_requestedThumbnailTime.exchange(-1.0);

            if (time < 0) {
                m_isThumbnailWorkerRunning.store(false);
                co_return;
            }

            co_await winrt::resume_background();
            std::vector<uint8_t> pixelData = m_ffmpegPlayer->ExtractThumbnail(time, 160, 90); // TODO: remove magic numbers
            co_await wil::resume_foreground(queue);

            if (pixelData.empty()) continue;

            SoftwareBitmap swBitmap(BitmapPixelFormat::Bgra8, 160, 90, BitmapAlphaMode::Premultiplied);
            // using scope for RAII destruction
            {
                // locking buffer to not draw it before it's initialized
                BitmapBuffer bitmapBuffer = swBitmap.LockBuffer(BitmapBufferAccessMode::Write);
                auto reference = bitmapBuffer.CreateReference();
                auto byteAccess = reference.as<::Windows::Foundation::IMemoryBufferByteAccess>();

                uint8_t* destPixels = nullptr;
                uint32_t capacity = 0;
                winrt::check_hresult(byteAccess->GetBuffer(&destPixels, &capacity));

                memcpy(destPixels, pixelData.data(), pixelData.size());
            }
            SoftwareBitmapSource source;
            co_await source.SetBitmapAsync(swBitmap);
            ThumbnailImage().Source(source);
        }
    }

    void MainWindow::OnTogglePipClick(IInspectable const&, RoutedEventArgs const&) {
        if (AppWindow().Presenter().Kind() == Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay) {
            AppWindow().SetPresenter(Microsoft::UI::Windowing::AppWindowPresenterKind::Default);
            MainUI().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
            PipUI().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        }
        else {
            AppWindow().SetPresenter(Microsoft::UI::Windowing::AppWindowPresenterKind::CompactOverlay);
            MainUI().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
            PipUI().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> MainWindow::GetToken() {
        Windows::Web::Http::HttpClient client;

        winrt::Windows::Web::Http::HttpFormUrlEncodedContent content({
            { L"client_id", L"65f0e9de-f4cc-4aec-8dd6-5496ab70caf4" },
            // TODO: add caching
            { L"scope", L"offline_access Files.Read.All" }
        });

        auto response = co_await client.PostAsync(Windows::Foundation::Uri(L"https://login.microsoftonline.com/common/oauth2/v2.0/devicecode"), content);
        if (!response.IsSuccessStatusCode()) co_return L"";

        Windows::Data::Json::JsonObject json {nullptr};
        if (!Windows::Data::Json::JsonObject::TryParse(co_await response.Content().ReadAsStringAsync(), json)) co_return L"";

        co_await wil::resume_foreground(DispatcherQueue());

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());

        winrt::Microsoft::UI::Xaml::Controls::TextBox urlBox;
        Microsoft::UI::Xaml::Controls::TextBox codeBox;
        urlBox.Text(json.GetNamedString(L"verification_uri"));
        codeBox.Text(json.GetNamedString(L"user_code"));
        urlBox.IsReadOnly(true);
        codeBox.IsReadOnly(true);
        Microsoft::UI::Xaml::Controls::StackPanel panel;
        panel.Children().Append(urlBox);
        panel.Children().Append(codeBox);

        dialog.Content(panel);
        dialog.CloseButtonText(L"Cancel");

        auto dialogOp = dialog.ShowAsync();

        co_await winrt::resume_background();

        Windows::Web::Http::HttpFormUrlEncodedContent tokenContent(
            {
                { L"client_id", L"65f0e9de-f4cc-4aec-8dd6-5496ab70caf4" },
                { L"grant_type", L"urn:ietf:params:oauth:grant-type:device_code" },
                { L"device_code", json.GetNamedString(L"device_code") }
            }
        );

        Windows::Foundation::Uri tokenUri(L"https://login.microsoftonline.com/common/oauth2/v2.0/token");

        while (true) {
            co_await winrt::resume_after(std::chrono::seconds(5));

            auto tokenResponse = co_await client.PostAsync(tokenUri, tokenContent);
            hstring tokenJsonStr = co_await tokenResponse.Content().ReadAsStringAsync();
            Windows::Data::Json::JsonObject tokenJson{ nullptr };

            if (tokenResponse.IsSuccessStatusCode() && Windows::Data::Json::JsonObject::TryParse(tokenJsonStr, tokenJson)) {
                co_await wil::resume_foreground(DispatcherQueue());
                dialogOp.Cancel();
                co_return tokenJson.GetNamedString(L"access_token");
            }

            if (to_string(tokenJsonStr).find("authorization_pending") == std::string::npos) break;
        }
        co_await wil::resume_foreground(DispatcherQueue());
        dialogOp.Cancel();
        co_return L"";
    }

    Windows::Foundation::IAsyncOperation<hstring> MainWindow::GetOneDriveUrl(hstring url) {
        auto buffer = winrt::Windows::Security::Cryptography::CryptographicBuffer::ConvertStringToBinary(url, winrt::Windows::Security::Cryptography::BinaryStringEncoding::Utf8);
        std::wstring encoded = winrt::Windows::Security::Cryptography::CryptographicBuffer::EncodeToBase64String(buffer).c_str();

        std::replace(encoded.begin(), encoded.end(), L'+', L'-');
        std::replace(encoded.begin(), encoded.end(), L'/', L'_');
        encoded.erase(std::remove(encoded.begin(), encoded.end(), L'='), encoded.end());

        winrt::hstring token = co_await GetToken();
        if (token.empty()) co_return L"";

        winrt::Windows::Web::Http::HttpClient client;
        client.DefaultRequestHeaders().Authorization(winrt::Windows::Web::Http::Headers::HttpCredentialsHeaderValue(L"Bearer", token));

        auto response = co_await client.GetAsync(winrt::Windows::Foundation::Uri(
            L"https://graph.microsoft.com/v1.0/shares/u!" + winrt::hstring(encoded) + L"/driveItem"
        ));

        winrt::Windows::Data::Json::JsonObject json{ nullptr };
        if (response.IsSuccessStatusCode() && winrt::Windows::Data::Json::JsonObject::TryParse(co_await response.Content().ReadAsStringAsync(), json)) {
            if (json.HasKey(L"@microsoft.graph.downloadUrl")) {
                co_return json.GetNamedString(L"@microsoft.graph.downloadUrl");
            }
        }
        co_return L"";
    }
}