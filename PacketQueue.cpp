#include "pch.h"
#include "PacketQueue.h"

void PacketQueue::Push(AVPacket* pkt) {
	std::unique_lock<std::mutex> lock(m_mutex);
	m_condPush.wait(lock, [this] { return m_queue.size() < m_capacity || m_abort; });
	if (m_abort) { av_packet_free(&pkt); return; }
	m_queue.push(pkt);
	m_condPop.notify_one();
}

bool PacketQueue::TryPush(AVPacket* pkt) {
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_abort || m_queue.size() >= m_capacity) return false;
	m_queue.push(pkt);
	m_condPop.notify_one();
	return true;
}

AVPacket* PacketQueue::Pop() {
	std::unique_lock<std::mutex> lock(m_mutex);
	m_condPop.wait(lock, [this] { return !m_queue.empty() || m_abort; });
	if (m_abort || m_queue.empty()) return nullptr;
	AVPacket* pkt = m_queue.front();
	m_queue.pop();
	m_condPush.notify_one();
	return pkt;
}

void PacketQueue::Clear() {
	std::lock_guard<std::mutex> lock(m_mutex);
	while (!m_queue.empty()) {
		AVPacket* pkt = m_queue.front();
		m_queue.pop();
		av_packet_free(&pkt);
	}
	m_condPush.notify_one();
}

void PacketQueue::Abort() {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_abort = true;
	m_condPush.notify_all();
	m_condPop.notify_all();
}

void PacketQueue::Reset() {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_abort = false;
}