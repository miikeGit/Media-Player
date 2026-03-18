#include "pch.h"

#include "TorrentClient.h"
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <thread>
#include <chrono>

using namespace lt;

TorrentClient::TorrentClient() {}
TorrentClient::~TorrentClient() {}

std::string TorrentClient::PlayMagnet(const std::string& magnet_url) {
    add_torrent_params params = parse_magnet_uri(magnet_url);
    params.save_path = "tmp";
    m_handle = m_session.add_torrent(std::move(params));
    
    while (!m_handle.status().has_metadata) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    FindTargetMedia();
    return m_targetFileName;
}

void TorrentClient::FindTargetMedia() {
    if (!m_handle.torrent_file()) return;

    int64_t max = 0;
    int maxIndex = -1;

    for (int i = 0; i < m_handle.torrent_file()->num_files(); i++) {
        int64_t file_size = m_handle.torrent_file()->files().file_size(file_index_t(i));
        if (file_size > max) {
            max = file_size;
            maxIndex = i;
        }
    }

    if (maxIndex != -1) {
        m_targetFileIndex = maxIndex;
        m_targetFileSize = max;
        m_targetFileName = m_handle.torrent_file()->files().file_name(file_index_t(maxIndex)).to_string();
    }
}