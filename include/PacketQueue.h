#pragma once

#include <queue>

struct AVPacket;

class PacketQueue {
public:
    void Push(AVPacket* pkt);
    AVPacket* Pop();
    void Clear();
    void Abort();
    void Reset();
    bool Empty() {
        return m_queue.empty();
    }

private:
    std::queue<AVPacket*> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_condPush; // when space available
    std::condition_variable m_condPop;  // when item available
    size_t m_capacity = 100;
    bool m_abort = false;
};