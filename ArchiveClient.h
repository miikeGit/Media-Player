#pragma once
#include <string>

struct archive;

class ArchiveClient {
public:
    ArchiveClient() = default;
    ~ArchiveClient() { Close(); }

    bool Open(const std::string& zipPath);
    void Close();

    static int ReadCallback(void* opaque, uint8_t* buf, int size);
    static int64_t SeekCallback(void* opaque, int64_t offset, int whence);

private:
    archive* m_archive = nullptr;
    int64_t m_fileSize = 0;
    int64_t m_position = 0;
    std::string m_zipPath;
    std::string m_entryName;

    archive* CreateArchive(const std::string& path);
    bool GoToEntryAndSkip(int64_t offset);
};