#pragma once

#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>

class TorrentClient {
public:
    TorrentClient() = default;
    ~TorrentClient() { Stop(); }
    
    void Stop();
    std::string PlayMagnet(const std::string& magnet);

    static constexpr std::string_view TMP_FOLDER_NAME = "LAMP_TMP";
private:
    std::string FindTargetMedia();

    lt::session m_session;
    lt::torrent_handle m_handle;
};