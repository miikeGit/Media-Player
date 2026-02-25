#include "pch.h"

#include "FFmpegPlayer.h"

using namespace winrt;

FFmpegPlayer::FFmpegPlayer() {
	InitializeDirectX();
}

FFmpegPlayer::~FFmpegPlayer() {
	Stop();
	CleanupFFmpeg();
}

void FFmpegPlayer::CleanupFFmpeg() {
	if (m_videoCodecContext) avcodec_free_context(&m_videoCodecContext);
	if (m_audioCodecContext) avcodec_free_context(&m_audioCodecContext);
	if (m_swsContext) { sws_freeContext(m_swsContext); m_swsContext = nullptr; }
	if (m_swrContext) { swr_free(&m_swrContext); m_swrContext = nullptr; }
	if (m_formatContext) avformat_close_input(&m_formatContext);
	if (m_frameBuffer) { av_free(m_frameBuffer); m_frameBuffer = nullptr; }

	{
		std::lock_guard<std::mutex> lock(m_audioMutex);
		m_audioQueue.clear();
	}

	m_videoTexture = nullptr;
	m_videoStreamIndex = -1;
	m_audioStreamIndex = -1;
	m_audioSampleRate = 0;
	m_audioChannels = 0;
	m_videoWidth = 0;
	m_videoHeight = 0;
	m_duration = 0.0;
	m_currentTime = 0.0;
	m_isPlaying = false;
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

void FFmpegPlayer::FindCodecs() {
	FindVideoCodec();
	FindAudioCodec();
}

void FFmpegPlayer::CreateD3D11Texture2DDesc() {
	D3D11_TEXTURE2D_DESC texDesc{};
	texDesc.Width = m_videoWidth;
	texDesc.Height = m_videoHeight;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	check_hresult(m_d3dDevice->CreateTexture2D(&texDesc, nullptr, m_videoTexture.put()));
}

void FFmpegPlayer::OpenAndPlay(const hstring& path) {
	Stop();
	CleanupFFmpeg();

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
		check_hresult(m_swapChain->ResizeBuffers(2, m_videoWidth, m_videoHeight, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
		check_hresult(m_swapChain->GetBuffer(0, IID_PPV_ARGS(m_backBuffer.put())));
	}

	ApplyMatrixTransform();
	FireEvent(MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA);

	m_currentTime = 0.0;
	m_isPlaying = true;
	m_decodeThread = std::thread(&FFmpegPlayer::DecodeThreadFunc, this);

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

void FFmpegPlayer::CheckIfSeeking(double& startPts, std::chrono::nanoseconds& pauseDuration) {
	if (m_shouldSeek.exchange(false)) {
		double target = m_seekTarget.load() * AV_TIME_BASE;

		if (av_seek_frame(m_formatContext, -1, target, AVSEEK_FLAG_BACKWARD) >= 0) {
			if (m_videoCodecContext) avcodec_flush_buffers(m_videoCodecContext);
			if (m_audioCodecContext) avcodec_flush_buffers(m_audioCodecContext);

			{
				std::lock_guard<std::mutex> lock(m_audioMutex);
				m_audioQueue.clear();
			}

			startPts = -1.0;
			pauseDuration = std::chrono::nanoseconds(0);
		}
	}
}

void FFmpegPlayer::DecodeAudioFrame(AVFrame* frame) {
	// (delay + input samples) * target sample rate / input sample rate, rounded up
	int maxOutSamples = static_cast<int>(av_rescale_rnd(
		swr_get_delay(m_swrContext, m_audioCodecContext->sample_rate) + frame->nb_samples,
		m_audioSampleRate, m_audioCodecContext->sample_rate, AV_ROUND_UP
	));

	std::vector<float> samples(maxOutSamples * m_audioChannels);
	// force to use one output plane with interleaved data
	uint8_t* outData[1] = { reinterpret_cast<uint8_t*>(samples.data()) };

	int converted = swr_convert(
		m_swrContext,
		outData, maxOutSamples,
		const_cast<const uint8_t**>(frame->data), frame->nb_samples
	);
	if (converted <= 0) return;

	// trim unnecessary space
	samples.resize(converted * m_audioChannels);

	std::lock_guard<std::mutex> lock(m_audioMutex);
	m_audioQueue.push_back(std::move(samples));
}

void FFmpegPlayer::DecodeThreadFunc() {
	AVPacket* packet = av_packet_alloc();
	AVFrame* videoFrame = av_frame_alloc();
	AVFrame* audioFrame = av_frame_alloc();

	// function expects arrays of size 4, even though we are only using one plane
	uint8_t* dstData[4] = {};
	int dstLinesize[4] = {};
	av_image_fill_arrays(dstData, dstLinesize, m_frameBuffer, AV_PIX_FMT_BGRA, m_videoWidth, m_videoHeight, 1);

	std::chrono::steady_clock::time_point playbackStart;
	double startPts = -1.0;
	std::chrono::nanoseconds totalPauseDuration{ 0 };

	while (av_read_frame(m_formatContext, packet) >= 0 && !m_isStopping.load()) {
		CheckIfPaused(totalPauseDuration);
		CheckIfSeeking(startPts, totalPauseDuration);

		if (packet->stream_index == m_videoStreamIndex && avcodec_send_packet(m_videoCodecContext, packet) == 0) {
			while (avcodec_receive_frame(m_videoCodecContext, videoFrame) == 0) { // returns 0 when frame is ready
				double pts = videoFrame->pts * av_q2d(m_formatContext->streams[m_videoStreamIndex]->time_base); // convert to seconds

				if (startPts < 0.0) { // if it's the first frame
					startPts = pts;
					playbackStart = std::chrono::steady_clock::now();
					totalPauseDuration = std::chrono::nanoseconds{ 0 };
				}

				auto targetTime = playbackStart + totalPauseDuration + std::chrono::duration<double>(pts - startPts);
				std::this_thread::sleep_until(targetTime);

				{
					std::lock_guard<std::mutex> lock(m_frameMutex);
					sws_scale(m_swsContext, videoFrame->data, videoFrame->linesize, 0, m_videoHeight, dstData, dstLinesize); // convert to BGRA
					m_currentTime = pts;
				}
			}
		} else if (packet->stream_index == m_audioStreamIndex && avcodec_send_packet(m_audioCodecContext, packet) == 0) {
			while (avcodec_receive_frame(m_audioCodecContext, audioFrame) == 0) {
				DecodeAudioFrame(audioFrame);
			}
		}
		av_packet_unref(packet);
	}
	av_frame_free(&audioFrame);
	av_frame_free(&videoFrame);
	av_packet_free(&packet);
}

void FFmpegPlayer::RenderFrame() {
	if (!m_swapChain || !m_backBuffer) return;

	std::lock_guard<std::mutex> lock(m_frameMutex);
	if (!m_videoTexture || !m_frameBuffer) return;

	m_d3dDeviceContext->UpdateSubresource(m_videoTexture.get(), 0, nullptr, m_frameBuffer, m_videoWidth * 4, 0);
	m_d3dDeviceContext->CopyResource(m_backBuffer.get(), m_videoTexture.get());
	m_swapChain->Present(0, 0);
}

void FFmpegPlayer::Resize(UINT width, UINT height) {
	m_displayWidth = width;
	m_displayHeight = height;
	ApplyMatrixTransform();
}

void FFmpegPlayer::Play() {
	m_isPlaying = true;
	m_controlCV.notify_one();
	FireEvent(MF_MEDIA_ENGINE_EVENT_PLAYING);
}

void FFmpegPlayer::Pause() {
	m_isPlaying = false;
	FireEvent(MF_MEDIA_ENGINE_EVENT_PAUSE);
}

void FFmpegPlayer::Stop() {
	m_isPlaying = true;
	m_isStopping = true;
	m_controlCV.notify_one();

	if (m_decodeThread.joinable()) {
		m_decodeThread.join();
	}

	m_isPlaying = false;
	m_isStopping = false;

	CleanupFFmpeg();
	ClearFrame();
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