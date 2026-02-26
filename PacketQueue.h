#pragma once

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <array>
#include <vector>
#include <xaudio2.h>
#include <queue>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
}

class PacketQueue {
public:
    void Push(AVPacket* pkt);
    AVPacket* Pop();
    void Clear();
    void Abort();
    void Reset();

private:
    std::queue<AVPacket*> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_condPush; // when space available
    std::condition_variable m_condPop;  // when item available
    size_t m_capacity = 100;
    bool m_abort = false;
};