#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <MemoryBuffer.h>
#include <ShObjIdl_core.h> 
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.Windows.AppNotifications.h>

// suppress warnings for external header
#pragma warning(push, 0)
#include "srtparser.h"
#pragma warning(pop)

#include "MEPlayer.h"
#include "FFmpegPlayer.h"

#include <wil/cppwinrt_helpers.h>
#include <microsoft.ui.xaml.window.h>

using namespace wil;
using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Web::Http;
using namespace winrt::Windows::Web::Http::Headers;
using namespace winrt::Windows::Storage::Pickers;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Data::Json;
using namespace winrt::Windows::Security::Cryptography;
using namespace winrt::Microsoft::UI::Windowing;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Microsoft::UI::Xaml::Controls::Primitives;
using namespace winrt::Microsoft::UI::Xaml::Media::Imaging;

namespace winrt::MediaPlayer::implementation {
    MainWindow::MainWindow() {
        InitializeComponent();
        ExtendsContentIntoTitleBar(true);
        SetWindowSubclass(GetWindowFromWindowId(AppWindow().Id()), WindowSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));

        Closed([this](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Microsoft::UI::Xaml::WindowEventArgs const&) {
                if (timer) timer.Stop();
            }
        );

        m_torrentClient = std::make_unique<TorrentClient>();

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
            box_value(Input::PointerEventHandler{ this, &MainWindow::TimeSlider_PointerPressed }),
            true);
        TimeSlider().AddHandler(
            UIElement::PointerReleasedEvent(),
            box_value(Input::PointerEventHandler{ this, &MainWindow::TimeSlider_PointerReleased }),
            true);
        TimeSlider().AddHandler(
            UIElement::PointerCaptureLostEvent(),
            box_value(Input::PointerEventHandler{ this, &MainWindow::TimeSlider_PointerCaptureLost }),
            true);

        InitializeTimer();

        m_playlistItems = single_threaded_observable_vector<hstring>();
        PlaylistView().ItemsSource(m_playlistItems);
    }

    void MainWindow::OnPlayerEvent(DWORD event, DWORD_PTR param1) {
        DispatcherQueue().TryEnqueue([this, event, param1]() {
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
                    m_player->SetVolume(VolumeSlider().Value() / 100.0);
                    auto w = static_cast<UINT>(SwapChainCanvas().ActualWidth());
                    auto h = static_cast<UINT>(SwapChainCanvas().ActualHeight());
                    if (w > 0 && h > 0) m_player->Resize(w, h);
                    if (!m_playlist.empty() && m_currentIndex >= 0)
                        m_player->OpenAndPlay(m_playlist[m_currentIndex]);
                }
                break;
            case MF_MEDIA_ENGINE_EVENT_ENDED:
                PlayPauseIcon().Symbol(Symbol::Play);
                PipPlayPauseIcon().Symbol(Symbol::Play);
                m_player->ClearFrame();
                if (m_currentIndex + 1 < static_cast<int>(m_playlist.size()))
                    PlayAtIndex(m_currentIndex + 1);
                break;
            case MF_MEDIA_ENGINE_EVENT_PLAYING:
                PlayPauseIcon().Symbol(Symbol::Pause);
                PipPlayPauseIcon().Symbol(Symbol::Pause);
                break;
            case MF_MEDIA_ENGINE_EVENT_PAUSE:
                PlayPauseIcon().Symbol(Symbol::Play);
                PipPlayPauseIcon().Symbol(Symbol::Play);
                break;
            case MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA:
                if (m_playlistItems.Size() != 0)
                    MediaTitle().Text(m_playlistItems.GetAt(m_currentIndex));
                break;
            }
            });
    }

    MainWindow::~MainWindow() {}

    void MainWindow::InitializeTimer() {
        auto queue = DispatcherQueue();
        timer = queue.CreateTimer();
        timer.Interval(std::chrono::milliseconds(15));
        timer.Tick({ this, &MainWindow::OnTimerTick });
        timer.Start();
    }

    hstring MainWindow::FormatTime(std::chrono::duration<double> time) {
        int totalSec = static_cast<int>(time.count());
        int hrs = totalSec / 3600;
        int mins = (totalSec % 3600) / 60;
        int secs = totalSec % 60;

        return hstring(std::format(L"{}:{:02}:{:02}", hrs, mins, secs));
    }

    void MainWindow::OnTimerTick(IInspectable const&, IInspectable const&) {
        if (!m_player) return;
        m_player->RenderFrame();

        auto duration = m_player->GetDuration();
        auto currentTime = m_player->GetCurrentTime();

        if (duration.count() > 0.0) {
            DurationText().Text(FormatTime(duration));
            CurrentTimeText().Text(FormatTime(currentTime));

            std::wstring sub = m_player->GetCurrentSubtitle(currentTime);
            if (sub.empty()) {
                SubtitleBorder().Visibility(Visibility::Collapsed);
            }
            else {
                SubtitleText().Text(hstring{ sub });
                SubtitleBorder().Visibility(Visibility::Visible);
            }

            if (!m_isSeeking) {
                TimeSlider().Maximum(duration.count());
                TimeSlider().Value(currentTime.count());
            }
        }
    }

    void MainWindow::TimeSlider_PointerPressed(IInspectable const&, PointerRoutedEventArgs const&) {
        m_isSeeking = true;
    }

    void MainWindow::TimeSlider_PointerReleased(IInspectable const&, PointerRoutedEventArgs const&) {
        if (m_isSeeking) {
            m_player->SetCurrentTime(std::chrono::duration<double>(TimeSlider().Value()));
            m_isSeeking = false;
        }
    }

    void MainWindow::TimeSlider_PointerCaptureLost(IInspectable const&, PointerRoutedEventArgs const&) {
        if (m_isSeeking) {
            m_player->SetCurrentTime(std::chrono::duration<double>(TimeSlider().Value()));
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
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, true };

        if (!CreatePipe(&read, &write, &sa, 0)) return "";
        // overwrite read pipe inheritance to false, otherwise UB
        SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{ sizeof(si) };
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = write;

        PROCESS_INFORMATION pi{};

        command.push_back(L'\0');
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
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

        CloseHandle(write);
        CloseHandle(read);
        return "";
    }

    fire_and_forget MainWindow::OnOpenUrlClick(IInspectable const&, RoutedEventArgs const&) {
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
                hstring directLink = co_await GetOneDriveUrl(hstring(inputUrl));
                resultUrl = to_string(directLink);
            }
            else {
                co_await resume_background();
                resultUrl = ExecCMD(L"yt-dlp.exe --no-warnings -g " + inputUrl);
            }
            co_await resume_foreground(DispatcherQueue());

            if (resultUrl.empty()) co_return;
            m_player->OpenAndPlay(to_hstring(resultUrl));
        }
    }

    void MainWindow::OnVolumeSliderValueChanged(IInspectable const&, RangeBaseValueChangedEventArgs const&) {
        if (!m_player) return;

        VolumeText().Text(std::to_wstring(static_cast<int>(VolumeSlider().Value())));
        m_player->SetVolume(VolumeSlider().Value() / 100.0);

        if (VolumeSlider().Value() == 0) VolumeIcon().Glyph(L"\xE74F");
        else if (VolumeSlider().Value() < 33) VolumeIcon().Glyph(L"\uE993");
        else if (VolumeSlider().Value() < 66) VolumeIcon().Glyph(L"\uE994");
        else VolumeIcon().Glyph(L"\uE995");
    }

    FileOpenPicker MainWindow::CreateFilePicker(const std::vector<std::wstring>& extensions) {
        FileOpenPicker picker;
        picker.as<IInitializeWithWindow>()->Initialize(GetWindowFromWindowId(AppWindow().Id()));
        for (const auto& ex : extensions) picker.FileTypeFilter().Append(ex);
        return picker;
    }

    void MainWindow::SwapChainCanvasSizeChanged(IInspectable const&, SizeChangedEventArgs const& e) {
        if (!m_player) return;

        m_player->Resize(
            static_cast<UINT>(e.NewSize().Width),
            static_cast<UINT>(e.NewSize().Height));
    }

    void MainWindow::OnVolumeUp(Input::KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const&) {
        double current = VolumeSlider().Value();
        VolumeSlider().Value(current + 5);
    }

    void MainWindow::OnVolumeDown(Input::KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const&) {
        double current = VolumeSlider().Value();
        VolumeSlider().Value(current - 5);
    }

    void MainWindow::OnPlayPauseKey(KeyboardAccelerator const&, KeyboardAcceleratorInvokedEventArgs const&) {
        TogglePlayback();
    }

    void MainWindow::OnPlayPauseBtnClick(IInspectable const&, RoutedEventArgs const&) {
        TogglePlayback();
    }

    void MainWindow::TogglePlayback() {
        if (PlayPauseIcon().Symbol() == Symbol::Play) {
            m_player->Play();
        }
        else {
            m_player->Pause();
        }
    }

    void MainWindow::OnPlaylistSelectionChanged(IInspectable const&, SelectionChangedEventArgs const&) {
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

        DurationText().Text(FormatTime(std::chrono::duration<double>(0.0)));
        CurrentTimeText().Text(FormatTime(std::chrono::duration<double>(0.0)));
        TimeSlider().Maximum(0.0);
        TimeSlider().Value(0.0);
        MediaTitle().Text(hstring{});
    }

    void MainWindow::OnRemoveFromPlaylistClick(IInspectable const& sender, RoutedEventArgs const&) {
        auto current = sender.as<DependencyObject>();
        int index = -1;
        while (current) {
            auto item = current.try_as<ListViewItem>();
            if (item) {
                index = PlaylistView().IndexFromContainer(item);
                break;
            }
            current = VisualTreeHelper::GetParent(current);
        }

        if (index == -1) return;

        if (m_playlist.size() == 1) {
            OnClearPlaylistClick(nullptr, nullptr);
            return;
        }

        bool wasPlaying = (index == m_currentIndex);
        if (index < m_currentIndex)
            m_currentIndex--;

        m_playlist.erase(m_playlist.begin() + index);
        m_playlistItems.RemoveAt(index);

        if (wasPlaying) {
            PlayAtIndex(
                (std::min)
                (index, static_cast<int>(m_playlist.size()) - 1)
            );
        }
    }

    void MainWindow::OnTempoItemClick(IInspectable const& sender, RoutedEventArgs const&) {
        if (!m_player) return;

        auto item = sender.as<RadioMenuFlyoutItem>();
        double speed = std::wcstod(unbox_value<hstring>(item.Tag()).c_str(), nullptr);
        m_player->SetPlaybackSpeed(speed);
        TempoButton().Content(box_value(item.Text()));
    }

    fire_and_forget MainWindow::OnOpenSubtitleFile(IInspectable const&, RoutedEventArgs const&) {
        auto file = co_await CreateFilePicker({ L".srt" }).PickSingleFileAsync();
        if (!file) co_return;

        std::unique_ptr<SubtitleParser> parser(
            SubtitleParserFactory(to_string(file.Path())).getParser()
        );

        std::vector<SubItem> subtitles;
        for (SubtitleItem* item : parser->getSubtitles()) {
            if (item->getIgnoreStatus()) continue;
            subtitles.push_back({
                std::chrono::duration<double>(item->getStartTime() / 1000.0),
                std::chrono::duration<double>(item->getEndTime() / 1000.0),
                std::wstring(to_hstring(item->getDialogue()))
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
        if (!m_player || !m_player->GetDuration().count()) return;

        if (m_player->IsClipRecording()) {
            m_player->StopClipRecording();
            ClipRecordButton().Text(L"Start recording");
        }
        else {
            m_player->StartClipRecording();
            ClipRecordButton().Text(L"Stop recording");
        }
    }

    void MainWindow::OnSliderPointerEntered(IInspectable const&, PointerRoutedEventArgs const&) {
        ThumbnailPopup().IsOpen(true);
    }

    void MainWindow::OnSliderPointerExited(IInspectable const&, PointerRoutedEventArgs const&) {
        ThumbnailPopup().IsOpen(false);
    }

    void MainWindow::OnSliderPointerMoved(IInspectable const& sender, PointerRoutedEventArgs const& e) {
        auto slider = sender.as<Slider>();

        double ratio = e.GetCurrentPoint(slider).Position().X / slider.ActualWidth();
        double target = ratio * m_player->GetDuration().count();

        ThumbnailPopup().HorizontalOffset(e.GetCurrentPoint(slider).Position().X);

        // overwrite
        m_requestedThumbnailTime.store(target);

        if (!m_isThumbnailWorkerRunning.exchange(true)) {
            RunThumbnailWorkerAsync();
        }
    }

    IAsyncAction MainWindow::RunThumbnailWorkerAsync() {
        auto queue = DispatcherQueue();

        while (true) {
            double time = m_requestedThumbnailTime.exchange(-1.0);

            if (time < 0) {
                m_isThumbnailWorkerRunning.store(false);
                co_return;
            }

            co_await resume_background();
            std::vector<uint8_t> pixelData = m_ffmpegPlayer->ExtractThumbnail(std::chrono::duration<double>(time), 160, 90); // TODO: remove magic numbers
            co_await resume_foreground(queue);

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
                check_hresult(byteAccess->GetBuffer(&destPixels, &capacity));

                memcpy(destPixels, pixelData.data(), pixelData.size());
            }
            SoftwareBitmapSource source;
            co_await source.SetBitmapAsync(swBitmap);
            ThumbnailImage().Source(source);
        }
    }

    void MainWindow::OnTogglePipClick(IInspectable const&, RoutedEventArgs const&) {
        if (AppWindow().Presenter().Kind() == AppWindowPresenterKind::CompactOverlay) {
            AppWindow().SetPresenter(AppWindowPresenterKind::Default);
            MainUI().Visibility(Visibility::Visible);
            PipUI().Visibility(Visibility::Collapsed);
            m_PiPModeEnabled = false;
        }
        else {
            m_PiPModeEnabled = true;
            AppWindow().SetPresenter(AppWindowPresenterKind::CompactOverlay);
            MainUI().Visibility(Visibility::Collapsed);
            PipUI().Visibility(Visibility::Visible);
        }
    }

    IAsyncOperation<hstring> MainWindow::GetToken() {
        HttpClient client;

        HttpFormUrlEncodedContent content({
            { L"client_id", L"65f0e9de-f4cc-4aec-8dd6-5496ab70caf4" },
            // TODO: add caching
            { L"scope", L"offline_access Files.Read.All" }
            });

        auto response = co_await client.PostAsync(
            Uri(L"https://login.microsoftonline.com/common/oauth2/v2.0/devicecode"), content
        );
        if (!response.IsSuccessStatusCode()) co_return L"";

        JsonObject json{ nullptr };
        if (!JsonObject::TryParse(co_await response.Content().ReadAsStringAsync(), json)) co_return L"";

        co_await resume_foreground(DispatcherQueue());

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());

        TextBox urlBox;
        TextBox codeBox;
        urlBox.Text(json.GetNamedString(L"verification_uri"));
        codeBox.Text(json.GetNamedString(L"user_code"));
        urlBox.IsReadOnly(true);
        codeBox.IsReadOnly(true);
        StackPanel panel;
        panel.Children().Append(urlBox);
        panel.Children().Append(codeBox);

        dialog.Content(panel);
        dialog.CloseButtonText(L"Cancel");

        auto dialogOp = dialog.ShowAsync();

        co_await resume_background();

        HttpFormUrlEncodedContent tokenContent(
            {
                { L"client_id", L"65f0e9de-f4cc-4aec-8dd6-5496ab70caf4" },
                { L"grant_type", L"urn:ietf:params:oauth:grant-type:device_code" },
                { L"device_code", json.GetNamedString(L"device_code") }
            }
        );

        Uri tokenUri(L"https://login.microsoftonline.com/common/oauth2/v2.0/token");

        while (true) {
            co_await resume_after(std::chrono::seconds(5));

            auto tokenResponse = co_await client.PostAsync(tokenUri, tokenContent);
            hstring tokenJsonStr = co_await tokenResponse.Content().ReadAsStringAsync();
            JsonObject tokenJson{ nullptr };

            if (tokenResponse.IsSuccessStatusCode() && JsonObject::TryParse(tokenJsonStr, tokenJson)) {
                co_await resume_foreground(DispatcherQueue());
                dialogOp.Cancel();
                co_return tokenJson.GetNamedString(L"access_token");
            }

            if (to_string(tokenJsonStr).find("authorization_pending") == std::string::npos) break;
        }
        co_await resume_foreground(DispatcherQueue());
        dialogOp.Cancel();
        co_return L"";
    }

    IAsyncOperation<hstring> MainWindow::GetOneDriveUrl(hstring url) {
        auto buffer = CryptographicBuffer::ConvertStringToBinary(url, BinaryStringEncoding::Utf8);
        std::wstring encoded = CryptographicBuffer::EncodeToBase64String(buffer).c_str();

        std::replace(encoded.begin(), encoded.end(), L'+', L'-');
        std::replace(encoded.begin(), encoded.end(), L'/', L'_');
        encoded.erase(std::remove(encoded.begin(), encoded.end(), L'='), encoded.end());

        hstring token = co_await GetToken();
        if (token.empty()) co_return L"";

        HttpClient client;
        client.DefaultRequestHeaders().Authorization(HttpCredentialsHeaderValue(L"Bearer", token));

        auto response = co_await client.GetAsync(Uri(
            L"https://graph.microsoft.com/v1.0/shares/u!" + hstring(encoded) + L"/driveItem"
        ));

        JsonObject json{ nullptr };
        if (response.IsSuccessStatusCode() && JsonObject::TryParse(co_await response.Content().ReadAsStringAsync(), json)) {
            if (json.HasKey(L"@microsoft.graph.downloadUrl")) {
                co_return json.GetNamedString(L"@microsoft.graph.downloadUrl");
            }
        }
        co_return L"";
    }

    void MainWindow::OnEffectItemClick(IInspectable const& sender, RoutedEventArgs const&) {
        auto item = sender.as<RadioMenuFlyoutItem>();

        hstring tag = unbox_value<hstring>(item.Tag());

        if (tag == L"Normal") {
            m_ffmpegPlayer->SetVideoEffect(VideoEffect::Normal);
        }
        else if (tag == L"Grayscale") {
            m_ffmpegPlayer->SetVideoEffect(VideoEffect::Grayscale);
        }
    }

    void MainWindow::OnAudioEffectItemClick(IInspectable const& sender, RoutedEventArgs const&) {
        auto item = sender.as<RadioMenuFlyoutItem>();
        hstring tag = unbox_value<hstring>(item.Tag());

        if (tag == L"Normal") {
            m_ffmpegPlayer->SetAudioEffect(AudioEffect::Normal);
        }
        else if (tag == L"Reverb") {
            m_ffmpegPlayer->SetAudioEffect(AudioEffect::Reverb);
        }
    }

    fire_and_forget MainWindow::OnPlayFromMagnetClick(IInspectable const&, RoutedEventArgs const&) {
        TextBox input;
        input.PlaceholderText(L"Paste magnet link here");
        input.Width(450);

        ContentDialog dialog;
        dialog.Content(input);
        dialog.PrimaryButtonText(L"Play");
        dialog.CloseButtonText(L"Cancel");
        dialog.XamlRoot(Content().XamlRoot());

        auto result = co_await dialog.ShowAsync();
        if (result != ContentDialogResult::Primary) co_return;
        
        std::string magnet = to_string(input.Text());
        co_await resume_background();
        std::string targetFile = m_torrentClient->PlayMagnet(magnet);
        co_await resume_foreground(DispatcherQueue());

        if (!targetFile.empty()) {
            MediaTitle().Text(to_hstring(std::filesystem::path(targetFile).filename().string()));
            m_player->OpenAndPlay(to_hstring(targetFile));
        }
        else {
            // TODO: handle this
        }
    }

    fire_and_forget MainWindow::OnPlayFromTrackerClick(IInspectable const&, RoutedEventArgs const&) {
        auto file = co_await CreateFilePicker({L".torrent"}).PickSingleFileAsync();
        if (!file) co_return;
            
        co_await resume_background();
        std::string filePath = m_torrentClient->PlayFile(to_string(file.Path()));
        co_await resume_foreground(DispatcherQueue());
        if (filePath.empty()) co_return;

        MediaTitle().Text(to_hstring(std::filesystem::path(filePath).filename().string()));
        m_player->OpenAndPlay(to_hstring(filePath));
    }

    fire_and_forget MainWindow::OnPlayFromZipClick(IInspectable const&, RoutedEventArgs const&) {
        auto file = co_await CreateFilePicker({ L".zip" }).PickSingleFileAsync();
        if (!file) co_return;

        co_await resume_background();
        m_ffmpegPlayer->OpenFromArchive(winrt::to_string(file.Path()));
        co_await resume_foreground(DispatcherQueue());
        
        MediaTitle().Text(to_hstring(file.Name().c_str()));
    }

    void MainWindow::OnExitClick(IInspectable const&, RoutedEventArgs const&) {
        Application::Current().Exit();
    }

    LRESULT CALLBACK MainWindow::WindowSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
        if (uMsg == WM_GETMINMAXINFO && !reinterpret_cast<MainWindow*>(dwRefData)->m_PiPModeEnabled) {
            const int MIN_WINDOW_WIDTH = 600;
            const int MIN_WINDOW_HEIGHT = 400;
            // Windows defaults to 96 dpi
            const int DEFAULT_DPI = 96;

            auto mmi = reinterpret_cast<MINMAXINFO*>(lParam);

            // Account for different scalings
            // MulDiv multiplies the first two numbers, divides by the third
            mmi->ptMinTrackSize.x = MulDiv(MIN_WINDOW_WIDTH, GetDpiForWindow(hWnd), DEFAULT_DPI);
            mmi->ptMinTrackSize.y = MulDiv(MIN_WINDOW_HEIGHT, GetDpiForWindow(hWnd), DEFAULT_DPI);

            return 0;
        }
        else if (uMsg == WM_NCDESTROY) {
            RemoveWindowSubclass(hWnd, WindowSubclassProc, uIdSubclass);
        }
        // skip other messages
        return DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }
}