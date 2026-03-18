#pragma once

#include <string>
#include <memory>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>

class TorrentClient {
public:
    TorrentClient();
    ~TorrentClient();

    std::string PlayMagnet(const std::string& magnet_url);
private:
    lt::session m_session;
    lt::torrent_handle m_handle;

    int m_targetFileIndex = -1;
    std::int64_t m_targetFileSize = 0;
    std::string m_targetFileName;

    void FindTargetMedia();
};