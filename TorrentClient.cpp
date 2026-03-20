#include "pch.h"
#include "TorrentClient.h"
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

using namespace lt;
using namespace lt::torrent_flags;

std::string TorrentClient::PlayMagnet(const std::string& magnet) {
    auto tempPath = std::filesystem::temp_directory_path() / TMP_FOLDER_NAME;
    std::filesystem::remove_all(tempPath);
    std::filesystem::create_directories(tempPath);

    auto params = parse_magnet_uri(magnet);
    params.save_path = tempPath.string();
    params.flags |= sequential_download;
    m_handle = m_session.add_torrent(std::move(params));

    while (!m_handle.status().has_metadata)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::string relativePath = FindTargetMedia();
    if (relativePath.empty()) return "";

    while (true) {
        if (m_handle.status().total_done >= 10LL * 1024 * 1024 || 
            m_handle.status().total_done >= m_handle.status().total_wanted)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    return (std::filesystem::path(tempPath) / relativePath).string();
}

std::string TorrentClient::FindTargetMedia() {
    if (!m_handle.torrent_file()) return "";
    int64_t max = 0;
    int targetIndex = -1;

    for (int i = 0; i < m_handle.torrent_file()->num_files(); i++) {
        int64_t size = m_handle.torrent_file()->files().file_size(file_index_t(i));
        if (size > max) {
            max = size;
            targetIndex = i;
        }
    }

    if (targetIndex == -1) return "";

    std::vector<download_priority_t> priorities(m_handle.torrent_file()->num_files(), dont_download);
    priorities[targetIndex] = default_priority;
    m_handle.prioritize_files(priorities);

    return m_handle.torrent_file()->files().file_path(file_index_t(targetIndex));
}

void TorrentClient::Stop() {
    if (m_handle.is_valid()) {
        m_session.remove_torrent(m_handle, lt::session::delete_files);
        m_handle = {};
    }
}