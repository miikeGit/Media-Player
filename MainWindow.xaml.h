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

        void onOpenFileClick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    private:
        winrt::Microsoft::UI::Xaml::DispatcherTimer timer{ nullptr };

		com_ptr<ID3D11Device> d3dDevice;
        com_ptr<ID3D11DeviceContext> d3dDeviceContext;
        com_ptr<IDXGISwapChain1> swapChain;
		com_ptr<ID3D11Texture2D> backBuffer;
		com_ptr<ID3D11RenderTargetView> renderTargetView;
        com_ptr<IMFMediaEngine> mediaEngine;
        com_ptr<IMFDXGIDeviceManager> dxgiManager;

        UINT resetToken;

        void InitializeDirectX();
        void InitializeSwapChain();
        void InitializeMediaEngine();
        void InitializeTimer();

        void OnTimerTick(
            winrt::Windows::Foundation::IInspectable const& sender,
            winrt::Windows::Foundation::IInspectable const& e);

        fire_and_forget openFile();
    };
}

namespace winrt::MediaPlayer::factory_implementation{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
