
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
	if (m_swsContext) { sws_freeContext(m_swsContext); m_swsContext = nullptr; }
	if (m_formatContext) avformat_close_input(&m_formatContext);
	if (m_frameBuffer) { av_free(m_frameBuffer); m_frameBuffer = nullptr; }
	m_videoTexture = nullptr;
	m_videoStreamIndex = -1;
	m_videoWidth = 0;
	m_videoHeight = 0;
	m_duration = 0.0;
	m_currentTime = 0.0;
	m_isPlaying = false;
}

void FFmpegPlayer::FindStreamAndCodec() {
	for (int i = 0; i < m_formatContext->nb_streams; i++) {
		auto params = m_formatContext->streams[i]->codecpar;
		auto codec = avcodec_find_decoder(params->codec_id);
		if (!codec) continue;

		if (params->codec_type == AVMEDIA_TYPE_VIDEO && m_videoStreamIndex == -1) {
			m_videoStreamIndex = i;
			m_videoCodecContext = avcodec_alloc_context3(codec);
			avcodec_parameters_to_context(m_videoCodecContext, params);
			avcodec_open2(m_videoCodecContext, codec, nullptr);
			m_videoWidth = params->width;
			m_videoHeight = params->height;
		}
	}
	if (m_videoStreamIndex == -1) {
		FireEvent(MF_MEDIA_ENGINE_EVENT_ERROR);
		return;
	}
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

	FindStreamAndCodec();

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
			if (m_videoCodecContext) {
				avcodec_flush_buffers(m_videoCodecContext);
			}

			startPts = -1.0;
			pauseDuration = std::chrono::nanoseconds(0);
		}
	}
}

void FFmpegPlayer::DecodeThreadFunc() {
	AVPacket* packet = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();

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
			while (avcodec_receive_frame(m_videoCodecContext, frame) == 0) { // returns 0 when frame is ready
				double pts = frame->pts * av_q2d(m_formatContext->streams[m_videoStreamIndex]->time_base); // convert to seconds

				if (startPts < 0.0) { // if it's the first frame
					startPts = pts;
					playbackStart = std::chrono::steady_clock::now();
					totalPauseDuration = std::chrono::nanoseconds{ 0 };
				}

				auto targetTime = playbackStart + totalPauseDuration + std::chrono::duration<double>(pts - startPts);
				std::this_thread::sleep_until(targetTime);

				{
					std::lock_guard<std::mutex> lock(m_frameMutex);
					sws_scale(m_swsContext, frame->data, frame->linesize, 0, m_videoHeight, dstData, dstLinesize); // convert to BGRA
					m_currentTime = pts;
				}
			}
		}
		av_packet_unref(packet);
	}
	av_frame_free(&frame);
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

void FFmpegPlayer::Resize(UINT width, UINT height) {}

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