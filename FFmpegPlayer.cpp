#include "pch.h"

#include "FFmpegPlayer.h"

#include <wincodec.h>
#include <xaudio2.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>
#include <xaudio2fx.h>
#include <d3dcompiler.h>

extern "C" {
	#include <libswresample/swresample.h>
	#include <libswscale/swscale.h>
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libavutil/imgutils.h>
}

using namespace winrt;
using namespace Windows::Foundation;
using namespace winrt::Windows::ApplicationModel;

FFmpegPlayer::FFmpegPlayer() {
	InitializeDirectX();
	InitializeShaders();
}

FFmpegPlayer::~FFmpegPlayer() {
	Stop();
}

void FFmpegPlayer::InitializeAudio() {
	if (m_audioStreamIndex == -1) return;

	check_hresult(::XAudio2Create(m_xaudio2.put(), 0, XAUDIO2_DEFAULT_PROCESSOR));
	check_hresult(m_xaudio2->CreateMasteringVoice(&m_masteringVoice));

	WAVEFORMATEX wfx{};
	wfx.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
	wfx.nChannels = static_cast<WORD>(m_audioChannels);
	wfx.nSamplesPerSec = static_cast<DWORD>(m_audioSampleRate);
	wfx.wBitsPerSample = 32;
	wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

	check_hresult(m_xaudio2->CreateSourceVoice(&m_sourceVoice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO));

	com_ptr<::IUnknown> reverb;
	check_hresult(XAudio2CreateReverb(reverb.put(), 0));

	XAUDIO2_EFFECT_DESCRIPTOR descriptors[2] = {};
	descriptors[0].InitialState = FALSE;
	descriptors[0].OutputChannels = m_audioChannels;
	descriptors[0].pEffect = reverb.get();

	XAUDIO2_EFFECT_CHAIN effectChain{};
	effectChain.EffectCount = 1; // change if adding new
	effectChain.pEffectDescriptors = descriptors;
	check_hresult(m_sourceVoice->SetEffectChain(&effectChain));
	SetAudioEffect(m_currentAudioEffect.load());

	check_hresult(m_sourceVoice->Start(0));

	m_soundTouch.setSampleRate(m_audioSampleRate);
	m_soundTouch.setChannels(m_audioChannels);
	m_soundTouch.setTempo(m_playbackSpeed.load());
}

// reverb has index 0
void FFmpegPlayer::SetAudioEffect(AudioEffect effect) {
	m_currentAudioEffect = effect;

	if (!m_sourceVoice) return;

	switch (effect) {
	case AudioEffect::Normal:
		m_sourceVoice->DisableEffect(0);
		break;

	case AudioEffect::Reverb:
		m_sourceVoice->EnableEffect(0);
		break;
	}
}

void FFmpegPlayer::CleanupFFmpeg() {
	if (m_videoCodecContext) avcodec_free_context(&m_videoCodecContext);
	if (m_audioCodecContext) avcodec_free_context(&m_audioCodecContext);
	if (m_subtitleCodecContext) avcodec_free_context(&m_subtitleCodecContext);
	if (m_swsContext) { sws_freeContext(m_swsContext); m_swsContext = nullptr; }
	if (m_swrContext) { swr_free(&m_swrContext); m_swrContext = nullptr; }
	if (m_formatContext) avformat_close_input(&m_formatContext);
	if (m_frameBuffer) { av_free(m_frameBuffer); m_frameBuffer = nullptr; }
	
	{
		std::lock_guard<std::mutex> thumbLock(m_thumbnailMutex);
		if (m_thumbCodecContext) avcodec_free_context(&m_thumbCodecContext);
		if (m_thumbFormatContext) avformat_close_input(&m_thumbFormatContext);
		if (m_thumbSwsContext) { sws_freeContext(m_thumbSwsContext); m_thumbSwsContext = nullptr; } // ADDED
		if (m_thumbPacket) { av_packet_free(&m_thumbPacket); m_thumbPacket = nullptr; }             // ADDED
		if (m_thumbFrame) { av_frame_free(&m_thumbFrame); m_thumbFrame = nullptr; }                 // ADDED
		m_thumbnailStreamIndex = -1;
	}

	m_soundTouch.clear();
	
	{ std::lock_guard<std::mutex> lock(m_subtitleMutex); m_embeddedSubtitles.clear(); }

	{
		std::lock_guard<std::mutex> thumbLock(m_thumbnailMutex);
		if (m_thumbCodecContext) avcodec_free_context(&m_thumbCodecContext);
		if (m_thumbFormatContext) avformat_close_input(&m_thumbFormatContext);
		m_thumbnailStreamIndex = -1;
	}

	m_videoTexture = nullptr;
	m_videoStreamIndex = -1;
	m_audioStreamIndex = -1;
	m_subtitleStreamIndex = -1;
	m_audioSampleRate = 0;
	m_audioChannels = 0;
	m_videoWidth = 0;
	m_videoHeight = 0;
	m_duration = 0.0;
	m_currentTime = 0.0;
	m_isPlaying = false;

	if (m_sourceVoice) {
		m_sourceVoice->Stop(0);
		m_sourceVoice->FlushSourceBuffers();
		m_sourceVoice->DestroyVoice();
		m_sourceVoice = nullptr;
	}
	if (m_masteringVoice) {
		m_masteringVoice->DestroyVoice();
		m_masteringVoice = nullptr;
	}
	m_xaudio2 = nullptr;
}

void FFmpegPlayer::FindVideoCodec() {
	const AVCodec* videoCodec = nullptr;
	int videoStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &videoCodec, 0);

	if (videoStreamIndex < 0) {
		FireEvent(MF_MEDIA_ENGINE_EVENT_ERROR);
		return;
	}

	m_videoStreamIndex = videoStreamIndex;
	AVCodecParameters* params = m_formatContext->streams[videoStreamIndex]->codecpar;
	m_videoCodecContext = avcodec_alloc_context3(videoCodec);
	avcodec_parameters_to_context(m_videoCodecContext, params);

	m_videoCodecContext->thread_count = 0;
	m_videoCodecContext->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

	avcodec_open2(m_videoCodecContext, videoCodec, nullptr);
	m_videoWidth = params->width;
	m_videoHeight = params->height;
}

void FFmpegPlayer::FindAudioCodec() {
	const AVCodec* audioCodec = nullptr;
	int audioStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_AUDIO, -1, m_videoStreamIndex, &audioCodec, 0);

	if (audioStreamIndex < 0) {
		FireEvent(MF_MEDIA_ENGINE_EVENT_ERROR);
		return;
	}
	m_audioStreamIndex = audioStreamIndex;
	AVCodecParameters* audioParams = m_formatContext->streams[audioStreamIndex]->codecpar;

	m_audioCodecContext = avcodec_alloc_context3(audioCodec);
	avcodec_parameters_to_context(m_audioCodecContext, audioParams);
	avcodec_open2(m_audioCodecContext, audioCodec, nullptr);
	m_audioSampleRate = audioParams->sample_rate;
	m_audioChannels = 2;

	// swr context initialization for resampling and format conversion
	AVChannelLayout stereoLayout = AV_CHANNEL_LAYOUT_STEREO;
	swr_alloc_set_opts2(
		&m_swrContext,
		&stereoLayout,
		AV_SAMPLE_FMT_FLT, // interleaved
		m_audioSampleRate,
		&m_audioCodecContext->ch_layout,
		m_audioCodecContext->sample_fmt,
		m_audioCodecContext->sample_rate,
		0, nullptr // no loging
	);
	swr_init(m_swrContext);
}

void FFmpegPlayer::FindSubtitleCodec() {
	const AVCodec* subtitleCodec = nullptr;
	int subtitleStreamIndex = av_find_best_stream(m_formatContext, AVMEDIA_TYPE_SUBTITLE, -1, m_videoStreamIndex, &subtitleCodec, 0);

	if (subtitleStreamIndex < 0) return;

	m_subtitleStreamIndex = subtitleStreamIndex;
	AVCodecParameters* params = m_formatContext->streams[subtitleStreamIndex]->codecpar;
	m_subtitleCodecContext = avcodec_alloc_context3(subtitleCodec);
	avcodec_parameters_to_context(m_subtitleCodecContext, params);
	avcodec_open2(m_subtitleCodecContext, subtitleCodec, nullptr);
}

void FFmpegPlayer::FindCodecs() {
	FindVideoCodec();
	FindAudioCodec();
	FindSubtitleCodec();
}

void FFmpegPlayer::CreateD3D11Texture2DDesc() {
	D3D11_TEXTURE2D_DESC texDesc{};
	texDesc.Width = m_videoWidth;
	texDesc.Height = m_videoHeight;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.CPUAccessFlags = 0;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	check_hresult(m_d3dDevice->CreateTexture2D(&texDesc, nullptr, m_videoTexture.put()));
	check_hresult(m_d3dDevice->CreateShaderResourceView(m_videoTexture.get(), nullptr, m_videoSRV.put()));
}

void FFmpegPlayer::OpenAndPlay(const hstring& path) {
	Stop();
	CleanupFFmpeg();
	m_currentMediaPath = path.c_str();
	InitThumbnailDecoder();

	if (avformat_open_input(&m_formatContext, to_string(path).c_str(), nullptr, nullptr) != 0) {
		FireEvent(MF_MEDIA_ENGINE_EVENT_ERROR);
		return;
	}
	if (avformat_find_stream_info(m_formatContext, nullptr) < 0) {
		FireEvent(MF_MEDIA_ENGINE_EVENT_ERROR);
		return;
	}

	m_duration = static_cast<double>(m_formatContext->duration) / AV_TIME_BASE;
	FindCodecs();

	// context for rgba conversion
	m_swsContext = sws_getContext(
		m_videoWidth, m_videoHeight, m_videoCodecContext->pix_fmt,
		m_videoWidth, m_videoHeight, AV_PIX_FMT_BGRA,
		SWS_BILINEAR, nullptr, nullptr, nullptr
	);

	int bufSize = av_image_get_buffer_size(AV_PIX_FMT_BGRA, m_videoWidth, m_videoHeight, 1);
	m_frameBuffer = static_cast<uint8_t*>(av_malloc(bufSize));

	CreateD3D11Texture2DDesc();

	if (m_swapChain) {
		m_backBuffer = nullptr;
		m_renderTargetView = nullptr;
		check_hresult(m_swapChain->ResizeBuffers(2, m_videoWidth, m_videoHeight, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
		check_hresult(m_swapChain->GetBuffer(0, IID_PPV_ARGS(m_backBuffer.put())));
		check_hresult(m_d3dDevice->CreateRenderTargetView(m_backBuffer.get(), nullptr, m_renderTargetView.put()));
	}

	ApplyMatrixTransform();
	InitializeAudio();
	FireEvent(MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA);

	m_currentTime = 0.0;
	m_isPlaying = true;
	m_isStopping = false;
	m_videoQueue.Reset();
	m_audioQueue.Reset();
	m_subtitleQueue.Reset();

	m_readThread = std::thread(&FFmpegPlayer::ReadThreadFunc, this);
	m_videoThread = std::thread(&FFmpegPlayer::VideoThreadFunc, this);
	m_audioThread = std::thread(&FFmpegPlayer::AudioThreadFunc, this);
	if (m_subtitleStreamIndex >= 0) m_subtitleThread = std::thread(&FFmpegPlayer::SubtitleThreadFunc, this);
	
	FireEvent(MF_MEDIA_ENGINE_EVENT_PLAYING);
}

void FFmpegPlayer::CheckIfPaused(std::chrono::nanoseconds& pauseDuration) {
	std::unique_lock<std::mutex> lock(m_controlMutex);
	if (!m_isPlaying.load()) {
		auto pauseBegin = std::chrono::steady_clock::now();
		m_controlCV.wait(lock, [this] {
			return m_isPlaying.load();
			});
		auto pauseEnd = std::chrono::steady_clock::now();
		pauseDuration += (pauseEnd - pauseBegin);
	}
}

void FFmpegPlayer::DecodeSubtitlePacket(AVPacket* packet) {
	if (!m_subtitleCodecContext) return;

	AVSubtitle sub{};
	int gotSub = 0;

	if (avcodec_decode_subtitle2(m_subtitleCodecContext, &sub, &gotSub, packet) < 0 || !gotSub)
		return;

	AVRational tb = m_formatContext->streams[m_subtitleStreamIndex]->time_base;
	double packetTime = packet->pts * av_q2d(tb);
	double startTime = packetTime + sub.start_display_time / 1000.0;
	double endTime = (sub.end_display_time > sub.start_display_time) 
		? packetTime + sub.end_display_time / 1000.0
	    : packetTime + packet->duration * av_q2d(tb);

	for (unsigned i = 0; i < sub.num_rects; ++i) {
		AVSubtitleRect* rect = sub.rects[i];
		std::string raw;

		if (rect->type == SUBTITLE_TEXT && rect->text) {
			raw = rect->text;
		} else if (rect->type == SUBTITLE_ASS && rect->ass) {
			// skip 8 commas
			const char* ptr = rect->ass;
			for (int c = 0; c < 8; ++c) {
				ptr = std::strchr(ptr, ',');
				if (!ptr) break;
				++ptr;
			}
			if (ptr) raw = ptr;
		}

		if (raw.empty()) continue;
		std::wstring text = std::wstring(winrt::to_hstring(raw));

		std::wstring clean;
		clean.reserve(text.size());
		for (int j = 0; j < text.size(); ++j) {
			if (text[j] == L'{') {
				while (j < text.size() && text[j] != L'}') ++j;
			} else if (text[j] == L'\\' && j + 1 < text.size()) {
				wchar_t next = text[j + 1];
				if (next == L'N' || next == L'n') {
					clean += L'\n';
					++j;
				}
				else if (next == L'h') {
					clean += L' ';
					++j;
				}
				else {
					clean += text[j];
				}
			} else {
				clean += text[j];
			}
		}

		if (!clean.empty()) {
			std::lock_guard<std::mutex> lock(m_subtitleMutex);
			m_embeddedSubtitles.push_back({ startTime, endTime, std::move(clean) });
		}
	}
	avsubtitle_free(&sub);
}

void FFmpegPlayer::SubtitleThreadFunc() {
	while (!m_isStopping.load()) {
		AVPacket* packet = m_subtitleQueue.Pop();
		if (!packet) break;

		if (packet->data == nullptr && packet->size == 0) {
			std::lock_guard<std::mutex> lock(m_subtitleMutex);
			m_embeddedSubtitles.clear();
			av_packet_free(&packet);
			continue;
		}

		DecodeSubtitlePacket(packet);
		av_packet_unref(packet);
		av_packet_free(&packet);
	}
}

void FFmpegPlayer::DecodeAudioFrame(AVFrame* frame) {
	if (!m_swrContext || !m_sourceVoice) return;

	if (m_audioSpeedChanged.exchange(false)) {
		m_soundTouch.clear();
		m_soundTouch.setTempo(m_playbackSpeed.load());
	}

	// (delay + input samples) * target sample rate / input sample rate, rounded up
	int maxSwrOut = static_cast<int>(av_rescale_rnd(
		swr_get_delay(m_swrContext, m_audioCodecContext->sample_rate) + frame->nb_samples,
		m_audioSampleRate, m_audioCodecContext->sample_rate, AV_ROUND_UP
	));

	m_swrTempBuf.resize(maxSwrOut * m_audioChannels);
	// force to use one output plane with interleaved data
	uint8_t* outData[1] = { reinterpret_cast<uint8_t*>(m_swrTempBuf.data()) };

	int converted = swr_convert(
		m_swrContext,
		outData, maxSwrOut,
		const_cast<const uint8_t**>(frame->data), frame->nb_samples
	);
	if (converted <= 0) return;

	m_soundTouch.putSamples(m_swrTempBuf.data(), static_cast<unsigned int>(converted));

	XAUDIO2_VOICE_STATE state;
	while (m_soundTouch.numSamples() > 0 && !m_isStopping.load()) {
		uint available = m_soundTouch.numSamples();

		do {
			m_sourceVoice->GetState(&state);
			if (state.BuffersQueued >= AUDIO_BUFFER_COUNT)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
		} while (state.BuffersQueued >= AUDIO_BUFFER_COUNT && !m_isStopping.load());

		if (m_isStopping.load()) return;

		auto& buf = m_audioBufferPool[m_audioPoolIndex % AUDIO_BUFFER_COUNT];
		buf.resize(available * m_audioChannels);

		uint received = m_soundTouch.receiveSamples(buf.data(), available);
		if (received == 0) break;

		XAUDIO2_BUFFER xbuf{};
		xbuf.AudioBytes = static_cast<UINT32>(received * m_audioChannels * sizeof(float));
		xbuf.pAudioData = reinterpret_cast<const BYTE*>(buf.data());
		m_sourceVoice->SubmitSourceBuffer(&xbuf);

		m_audioPoolIndex++;
	}
}

void FFmpegPlayer::CheckIfSeeking() {
	if (m_shouldSeek.exchange(false)) {
		int64_t target = static_cast<int64_t>(m_seekTarget.load() * AV_TIME_BASE);
		if (av_seek_frame(m_formatContext, -1, target, AVSEEK_FLAG_BACKWARD) >= 0) {
			m_videoQueue.Clear();
			m_audioQueue.Clear();
			m_subtitleQueue.Clear();
			{ std::lock_guard<std::mutex> lock(m_subtitleMutex); m_embeddedSubtitles.clear(); }
			AVPacket* flushPkt = av_packet_alloc();
			m_videoQueue.Push(flushPkt);
			flushPkt = av_packet_alloc();
			m_audioQueue.Push(flushPkt);
		}
	}
}

void FFmpegPlayer::ReadThreadFunc() {
	AVPacket* packet = nullptr;
	while (!m_isStopping.load()) {
		CheckIfSeeking();

		packet = av_packet_alloc();
		if (av_read_frame(m_formatContext, packet) >= 0) {
			if (packet->stream_index == m_videoStreamIndex)
				m_videoQueue.Push(packet);
			else if (packet->stream_index == m_audioStreamIndex)
				m_audioQueue.Push(packet);
			else if (packet->stream_index == m_subtitleStreamIndex)
				m_subtitleQueue.Push(packet);
			else
				av_packet_free(&packet);
		}
		else {
			av_packet_free(&packet);
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
}

void FFmpegPlayer::VideoThreadFunc() {
	AVFrame* videoFrame = av_frame_alloc();
	uint8_t* dstData[4] = {};
	int dstLinesize[4] = {};
	av_image_fill_arrays(dstData, dstLinesize, m_frameBuffer, AV_PIX_FMT_BGRA, m_videoWidth, m_videoHeight, 1);

	std::chrono::steady_clock::time_point playbackStart;
	double startPts = -1.0;
	std::chrono::nanoseconds totalPauseDuration{ 0 };

	while (!m_isStopping.load()) {
		AVPacket* packet = m_videoQueue.Pop();
		if (!packet) break;

		// check for flush packets after possible seek
		if (packet->data == nullptr && packet->size == 0) {
			avcodec_flush_buffers(m_videoCodecContext);
			startPts = -1.0;
			totalPauseDuration = std::chrono::nanoseconds{ 0 };
			av_packet_free(&packet);
			continue;
		}

		CheckIfPaused(totalPauseDuration);

		if (m_videoSpeedChanged.exchange(false)) {
			startPts = -1.0;
			totalPauseDuration = std::chrono::nanoseconds{ 0 };
		}

		if (startPts >= 0.0) {
			auto elapsed = std::chrono::steady_clock::now() - playbackStart - totalPauseDuration;
			double mediaTarget = startPts + std::chrono::duration<double>(elapsed).count() * m_playbackSpeed.load();
			double packetTime = packet->pts * av_q2d(m_formatContext->streams[m_videoStreamIndex]->time_base);

			if (mediaTarget - packetTime > 0.1) { // if packet is behind 100ms
				if (!(packet->flags & AV_PKT_FLAG_KEY)) { // drop non-keyframes
					av_packet_free(&packet);
					continue;
				}
				avcodec_flush_buffers(m_videoCodecContext);
				startPts = -1.0;
			}
		}

		if (avcodec_send_packet(m_videoCodecContext, packet) == 0) {
			while (avcodec_receive_frame(m_videoCodecContext, videoFrame) == 0) {
				double pts = videoFrame->pts * av_q2d(m_formatContext->streams[m_videoStreamIndex]->time_base);

				if (startPts < 0.0) {
					startPts = pts;
					playbackStart = std::chrono::steady_clock::now();
					totalPauseDuration = std::chrono::nanoseconds{ 0 };
				}

				auto targetTime = playbackStart + totalPauseDuration +
					std::chrono::duration<double>((pts - startPts) / m_playbackSpeed.load());
				std::this_thread::sleep_until(targetTime);

				{
					std::lock_guard<std::mutex> lock(m_frameMutex);
					sws_scale(m_swsContext, videoFrame->data, videoFrame->linesize, 0, m_videoHeight, dstData, dstLinesize);
					m_currentTime = pts;
				}
			}
		}
		av_packet_unref(packet);
		av_packet_free(&packet);
	}
	av_frame_free(&videoFrame);
}

void FFmpegPlayer::AudioThreadFunc() {
	AVFrame* audioFrame = av_frame_alloc();

	while (!m_isStopping.load()) {
		AVPacket* packet = m_audioQueue.Pop();
		if (!packet) break;

		// check for flush packets after possible seek
		if (packet->data == nullptr && packet->size == 0) {
			avcodec_flush_buffers(m_audioCodecContext);
			if (m_sourceVoice) {
				m_sourceVoice->FlushSourceBuffers();
			}
			m_soundTouch.clear();
			av_packet_free(&packet);
			continue;
		}

		if (avcodec_send_packet(m_audioCodecContext, packet) == 0) {
			while (avcodec_receive_frame(m_audioCodecContext, audioFrame) == 0) {
				DecodeAudioFrame(audioFrame);
			}
		}
		av_packet_unref(packet);
		av_packet_free(&packet);
	}
	av_frame_free(&audioFrame);
}

void FFmpegPlayer::RenderFrame() {
	if (!m_swapChain || !m_backBuffer || !m_renderTargetView) return;

	{
		std::lock_guard<std::mutex> lock(m_frameMutex);
		if (!m_videoTexture || !m_frameBuffer || !m_videoSRV) return;

		m_d3dDeviceContext->UpdateSubresource(m_videoTexture.get(), 0, nullptr, m_frameBuffer, m_videoWidth * 4, 0);
	}

	// breaks if passed directly
	auto rtv = m_renderTargetView.get();
	m_d3dDeviceContext->OMSetRenderTargets(1, &rtv, nullptr);

	D3D11_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(m_videoWidth);
	viewport.Height = static_cast<float>(m_videoHeight);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	m_d3dDeviceContext->RSSetViewports(1, &viewport);

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	m_d3dDeviceContext->ClearRenderTargetView(m_renderTargetView.get(), clearColor);

	m_d3dDeviceContext->VSSetShader(m_vertexShader.get(), nullptr, 0);

	switch (m_currentEffect.load()) {
	case VideoEffect::Grayscale:
		if (m_psGrayscale) m_d3dDeviceContext->PSSetShader(m_psGrayscale.get(), nullptr, 0);
		break;
	case VideoEffect::Normal:
	default:
		if (m_psNormal) m_d3dDeviceContext->PSSetShader(m_psNormal.get(), nullptr, 0);
		break;
	}

	auto srv = m_videoSRV.get();
	m_d3dDeviceContext->PSSetShaderResources(0, 1, &srv);

	auto sampler = m_samplerState.get();
	m_d3dDeviceContext->PSSetSamplers(0, 1, &sampler);

	m_d3dDeviceContext->IASetInputLayout(nullptr); // disabled for 2d
	// draw 2 triangles using same points
	m_d3dDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_d3dDeviceContext->Draw(4, 0);

	m_swapChain->Present(0, 0);
}

void FFmpegPlayer::Resize(UINT width, UINT height) {
	m_displayWidth = width;
	m_displayHeight = height;
	ApplyMatrixTransform();
}

void FFmpegPlayer::Play() {
	m_isPlaying = true;
	if (m_sourceVoice) m_sourceVoice->Start(0);
	m_controlCV.notify_all();
	FireEvent(MF_MEDIA_ENGINE_EVENT_PLAYING);
}

void FFmpegPlayer::Pause() {
	m_isPlaying = false;
	if (m_sourceVoice) m_sourceVoice->Stop(0);
	FireEvent(MF_MEDIA_ENGINE_EVENT_PAUSE);
}

void FFmpegPlayer::Stop() {
	m_isPlaying = true;
	m_isStopping = true;
	m_isClipRecording = false;
	m_controlCV.notify_all ();

	m_videoQueue.Abort();
	m_audioQueue.Abort();
	m_subtitleQueue.Abort();

	if (m_readThread.joinable()) m_readThread.join();
	if (m_videoThread.joinable()) m_videoThread.join();
	if (m_audioThread.joinable()) m_audioThread.join();
	if (m_subtitleThread.joinable()) m_subtitleThread.join();
	if (m_clipExportThread.joinable()) m_clipExportThread.join();

	m_isPlaying = false;
	m_isStopping = false;
	m_videoQueue.Clear();
	m_audioQueue.Clear();
	m_subtitleQueue.Clear();

	CleanupFFmpeg();
	ClearFrame();
}

void FFmpegPlayer::SetVolume(double volume) {
	if (m_sourceVoice) m_sourceVoice->SetVolume(static_cast<float>(volume));
}

void FFmpegPlayer::SetPlaybackSpeed(double speed) {
	m_playbackSpeed = speed;
	m_videoSpeedChanged = true;
	m_audioSpeedChanged = true;
}

double FFmpegPlayer::GetCurrentTime() const {
	return m_currentTime;
}

double FFmpegPlayer::GetDuration() const {
	return m_duration;
}

void FFmpegPlayer::SetCurrentTime(double time) {
	m_currentTime = time;
	m_seekTarget = time;
	m_shouldSeek = true;
}

void FFmpegPlayer::ApplyMatrixTransform() {
	if (!m_swapChain || m_videoWidth == 0 || m_videoHeight == 0 || m_displayWidth == 0 || m_displayHeight == 0) return;

	float scale = (std::min)(
		static_cast<float>(m_displayWidth) / m_videoWidth,
		static_cast<float>(m_displayHeight) / m_videoHeight
		);

	float scaledW = m_videoWidth * scale;
	float scaledH = m_videoHeight * scale;
	float offsetX = (m_displayWidth - scaledW) / 2.0f;
	float offsetY = (m_displayHeight - scaledH) / 2.0f;

	DXGI_MATRIX_3X2_F matrix = {
		scale, 0.0f,
		0.0f, scale,
		offsetX, offsetY
	};
	check_hresult(m_swapChain.as<IDXGISwapChain2>()->SetMatrixTransform(&matrix));
}

std::wstring FFmpegPlayer::GetCurrentSubtitle(double currentTime) {
	std::lock_guard<std::mutex> lock(m_subtitleMutex);
	for (const auto& sub : m_subtitles) {
		if (currentTime >= sub.startTime && currentTime <= sub.endTime)
			return sub.text;
	}
	for (const auto& sub : m_embeddedSubtitles) {
		if (currentTime >= sub.startTime && currentTime <= sub.endTime)
			return sub.text;
	}
	return L"";
}

void FFmpegPlayer::TakeScreenshot() {
	if (!m_frameBuffer || m_videoWidth == 0 || m_videoHeight == 0) return;

	com_ptr<IWICImagingFactory2> wicFactory;
	check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wicFactory.put())));

	std::filesystem::path directory = m_currentMediaPath.parent_path();
	std::filesystem::path fullPath = directory / L"screenshot.png";

	com_ptr<IWICStream> stream;
	check_hresult(wicFactory->CreateStream(stream.put()));
	check_hresult(stream->InitializeFromFilename(fullPath.c_str(), GENERIC_WRITE));

	com_ptr<IWICBitmapEncoder> encoder;
	check_hresult(wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
	check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));

	std::lock_guard<std::mutex> lock(m_frameMutex);
	com_ptr<IWICBitmapFrameEncode> frame;
	check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
	check_hresult(frame->Initialize(nullptr));
	check_hresult(frame->SetSize(m_videoWidth, m_videoHeight));

	WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
	check_hresult(frame->SetPixelFormat(&format));

	UINT row = m_videoWidth * 4; // 4 bytes per pixel (BGRA)
	UINT bufferSize = row * m_videoHeight;
	check_hresult(frame->WritePixels(m_videoHeight, row, bufferSize, m_frameBuffer));
	check_hresult(frame->Commit());
	check_hresult(encoder->Commit());
}

void FFmpegPlayer::StartClipRecording() {
	m_clipStartTime = m_currentTime;
	m_isClipRecording = true;
}

void FFmpegPlayer::StopClipRecording() {
	if (!m_isClipRecording.load()) return;
	m_isClipRecording = false;

	double clipEnd = m_currentTime;
	if (clipEnd <= m_clipStartTime) return;

	// finish previous recording
	if (m_clipExportThread.joinable()) m_clipExportThread.join();

	m_clipExportThread = std::thread(&FFmpegPlayer::ExportClip, this, static_cast<int64_t>(m_clipStartTime), clipEnd);
}

bool FFmpegPlayer::IsClipRecording() const {
	return m_isClipRecording.load();
}

void FFmpegPlayer::ExportClip(int64_t startTime, double endTime) {
	std::filesystem::path outPath =
		m_currentMediaPath.parent_path() /
		std::wstring(winrt::to_hstring(GuidHelper::CreateNewGuid()) + L".mp4");

	AVFormatContext* inFmt = nullptr;
	if (avformat_open_input(&inFmt, m_currentMediaPath.string().c_str(), nullptr, nullptr) < 0) return;
	if (avformat_find_stream_info(inFmt, nullptr) < 0) {
		avformat_close_input(&inFmt);
		return;
	}

	AVFormatContext* outFmt = nullptr;
	if (avformat_alloc_output_context2(&outFmt, nullptr, nullptr, outPath.string().c_str()) < 0) {
		avformat_close_input(&inFmt);
		return;
	}

	std::vector<int> streamMap(inFmt->nb_streams, -1);
	int j = 0;
	for (unsigned i = 0; i < inFmt->nb_streams; ++i) {
		AVCodecParameters* params = inFmt->streams[i]->codecpar;
		if (params->codec_type != AVMEDIA_TYPE_VIDEO && 
			params->codec_type != AVMEDIA_TYPE_AUDIO) continue;

		AVStream* outStream = avformat_new_stream(outFmt, nullptr);
		if (!outStream) continue;

		avcodec_parameters_copy(outStream->codecpar, params);
		// reset tag when converting from different extensions
		outStream->codecpar->codec_tag = 0;

		outStream->avg_frame_rate = inFmt->streams[i]->avg_frame_rate;
		outStream->r_frame_rate = inFmt->streams[i]->r_frame_rate;
		outStream->time_base = inFmt->streams[i]->time_base;

		streamMap[i] = j++;
	}

	if (avio_open(&outFmt->pb, outPath.string().c_str(), AVIO_FLAG_WRITE) < 0) {
		avformat_free_context(outFmt);
		avformat_close_input(&inFmt);
		return;
	}

	av_seek_frame(inFmt, -1, startTime * AV_TIME_BASE, AVSEEK_FLAG_BACKWARD);

	if (avformat_write_header(outFmt, nullptr) < 0) {
		avio_closep(&outFmt->pb);
		avformat_free_context(outFmt);
		avformat_close_input(&inFmt);
		return;
	}

	AVPacket* pkt = av_packet_alloc();
	std::vector<int64_t> start_dts(inFmt->nb_streams, AV_NOPTS_VALUE);

	while (av_read_frame(inFmt, pkt) >= 0) {
		// skip unneeded streams
		if (streamMap[pkt->stream_index] < 0) {
			av_packet_unref(pkt);
			continue;
		}

		double pktTime = pkt->pts * av_q2d(inFmt->streams[pkt->stream_index]->time_base);

		// skip packets past end mark
		if (pktTime > endTime) {
			av_packet_unref(pkt);
			break;
		}

		// use dts if possible for first frame
		if (start_dts[pkt->stream_index] == AV_NOPTS_VALUE) {
			start_dts[pkt->stream_index] = (pkt->dts != AV_NOPTS_VALUE) ? pkt->dts : pkt->pts;
		}

		// shift pts by calculated offset
		if (pkt->pts != AV_NOPTS_VALUE) pkt->pts -= start_dts[pkt->stream_index];
		if (pkt->dts != AV_NOPTS_VALUE) pkt->dts -= start_dts[pkt->stream_index];

		av_packet_rescale_ts(
			pkt, 
			inFmt->streams[pkt->stream_index]->time_base, 
			outFmt->streams[streamMap[pkt->stream_index]]->time_base);
		
		// assign new index
		pkt->stream_index = streamMap[pkt->stream_index];
		// reset byte position
		pkt->pos = -1;

		av_interleaved_write_frame(outFmt, pkt);
		av_packet_unref(pkt);
	}
	av_packet_free(&pkt);
	av_write_trailer(outFmt); // moov atom (metadata)
	avio_closep(&outFmt->pb);
	avformat_free_context(outFmt);
	avformat_close_input(&inFmt);
}

std::vector<uint8_t> FFmpegPlayer::ExtractThumbnail(double targetTime, int thumbWidth, int thumbHeight) {
	std::vector<uint8_t> pixelBuffer;

	std::lock_guard<std::mutex> lock(m_thumbnailMutex);
	if (!m_thumbFormatContext || !m_thumbCodecContext || m_thumbnailStreamIndex < 0) {
		return pixelBuffer;
	}

	int64_t targetTimestamp = static_cast<int64_t>(targetTime * AV_TIME_BASE);
	av_seek_frame(m_thumbFormatContext, -1, targetTimestamp, AVSEEK_FLAG_BACKWARD);
	avcodec_flush_buffers(m_thumbCodecContext);

	if (!m_thumbSwsContext) {
		m_thumbSwsContext = sws_getContext(
			m_thumbCodecContext->width, m_thumbCodecContext->height, m_thumbCodecContext->pix_fmt,
			thumbWidth, thumbHeight, AV_PIX_FMT_BGRA,
			SWS_BILINEAR, nullptr, nullptr, nullptr
		);
	}

	pixelBuffer.resize(av_image_get_buffer_size(AV_PIX_FMT_BGRA, thumbWidth, thumbHeight, 1));

	uint8_t* dstData[4] = { pixelBuffer.data() };
	int dstLinesize[4] = { thumbWidth * 4 };

	av_frame_unref(m_thumbFrame);
	av_packet_unref(m_thumbPacket);
	bool frameFound = false;

	// until we decode one frame
	while (av_read_frame(m_thumbFormatContext, m_thumbPacket) >= 0 && !frameFound) {
		if (m_thumbPacket->stream_index == m_thumbnailStreamIndex) {
			if (avcodec_send_packet(m_thumbCodecContext, m_thumbPacket) == 0) {
				if (avcodec_receive_frame(m_thumbCodecContext, m_thumbFrame) == 0) {
					sws_scale(m_thumbSwsContext, m_thumbFrame->data, m_thumbFrame->linesize, 0, m_thumbCodecContext->height, dstData, dstLinesize);
					frameFound = true;
				}
			}
		}
		av_packet_unref(m_thumbPacket);
	}
	return pixelBuffer;
}

void FFmpegPlayer::InitThumbnailDecoder() {
	std::lock_guard<std::mutex> lock(m_thumbnailMutex);

	if (avformat_open_input(&m_thumbFormatContext, m_currentMediaPath.string().c_str(), nullptr, nullptr) < 0) return;
	if (avformat_find_stream_info(m_thumbFormatContext, nullptr) < 0) return;

	const AVCodec* codec = nullptr;
	m_thumbnailStreamIndex = av_find_best_stream(m_thumbFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
	if (m_thumbnailStreamIndex < 0) return;

	m_thumbCodecContext = avcodec_alloc_context3(codec);
	avcodec_parameters_to_context(m_thumbCodecContext, m_thumbFormatContext->streams[m_thumbnailStreamIndex]->codecpar);
	m_thumbCodecContext->thread_count = 0;
	m_thumbCodecContext->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
	m_thumbCodecContext->skip_loop_filter = AVDISCARD_ALL;
	avcodec_open2(m_thumbCodecContext, codec, nullptr);

	if (!m_thumbPacket) m_thumbPacket = av_packet_alloc();
	if (!m_thumbFrame) m_thumbFrame = av_frame_alloc();
}

void FFmpegPlayer::InitializeShaders() {
	// can't find shaders without this
	std::wstring appDir = Package::Current().InstalledLocation().Path().c_str();

	com_ptr<ID3DBlob> vsBlob, psNormalBlob, psGrayBlob;

	D3DReadFileToBlob((appDir + L"\\VertexShader.cso").c_str(), vsBlob.put());
	m_d3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vertexShader.put());

	D3DReadFileToBlob((appDir + L"\\NormalShader.cso").c_str(), psNormalBlob.put());
	m_d3dDevice->CreatePixelShader(psNormalBlob->GetBufferPointer(), psNormalBlob->GetBufferSize(), nullptr, m_psNormal.put());

	D3DReadFileToBlob((appDir + L"\\GrayscaleShader.cso").c_str(), psGrayBlob.put());
	m_d3dDevice->CreatePixelShader(psGrayBlob->GetBufferPointer(), psGrayBlob->GetBufferSize(), nullptr, m_psGrayscale.put());

	D3D11_SAMPLER_DESC sampDesc{};
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER; // not needed for 2d
	m_d3dDevice->CreateSamplerState(&sampDesc, m_samplerState.put());
}

void FFmpegPlayer::SetVideoEffect(VideoEffect effect) {
	m_currentEffect = effect;
}