#include "pch.h"
#include "ArchiveClient.h"
#include <archive.h>
#include <archive_entry.h>
#include <vector>
#include <algorithm>

extern "C" {
#include <libavutil/error.h>
#include <libavformat/avio.h>
}

void ArchiveDeleter::operator()(archive* a) const { 
    if (a) archive_read_free(a); 
}

archive_ptr ArchiveClient::CreateArchive(const std::string& path) {
    archive_ptr a(archive_read_new());
    
    if (!a) return nullptr;

    archive_read_support_format_zip(a.get());
    
    if (archive_read_open_filename(a.get(), path.c_str(), 64 * 1024) != ARCHIVE_OK) {
        return nullptr;
    }

    return a;
}

void ArchiveClient::Close() {
    m_archive.reset();
    m_position = 0;
}

bool ArchiveClient::Open(const std::string& zipPath) {
    m_zipPath = zipPath;
    auto a(CreateArchive(zipPath));
    if (!a) return false;

    archive_entry* entry;
    int64_t maxSize = 0;

    // find biggest file
    while (archive_read_next_header(a.get(), &entry) == ARCHIVE_OK) {
        int64_t size = archive_entry_size(entry);
        if (size > maxSize) {
            maxSize = size;
            m_entryName = archive_entry_pathname(entry);
        }
        archive_read_data_skip(a.get());
    }
    if (m_entryName.empty()) return false;

    m_fileSize = maxSize;
    return GoToEntryAndSkip(0);
}

bool ArchiveClient::GoToEntryAndSkip(int64_t offset) {
    Close();
    m_archive = CreateArchive(m_zipPath);
    if (!m_archive) return false;

    archive_entry* entry;
    while (archive_read_next_header(m_archive.get(), &entry) == ARCHIVE_OK) {
        if (std::string(archive_entry_pathname(entry)) == m_entryName) {
            if (offset > 0) {
                std::vector<uint8_t> discard(64 * 1024);
                int64_t remaining = offset;
                while (remaining > 0) {
                    la_ssize_t got = archive_read_data(m_archive.get(), discard.data(), std::min(remaining, static_cast<int64_t>(discard.size())));
                    if (got <= 0) return false;
                    remaining -= got;
                }
            }
            m_position = offset;
            return true;
        }
        archive_read_data_skip(m_archive.get());
    }
    return false;
}

int ArchiveClient::ReadCallback(void* opaque, uint8_t* buf, int size) {
    auto* ctx = static_cast<ArchiveClient*>(opaque);
    if (!ctx->m_archive || ctx->m_position >= ctx->m_fileSize) return AVERROR_EOF;

    int toRead = static_cast<int>(std::min((int64_t)size, ctx->m_fileSize - ctx->m_position));
    la_ssize_t got = archive_read_data(ctx->m_archive.get(), buf, toRead);

    if (got < 0) return AVERROR(EIO);
    if (got == 0) return AVERROR_EOF;

    ctx->m_position += got;
    return static_cast<int>(got);
}

int64_t ArchiveClient::SeekCallback(void* opaque, int64_t offset, int startPos) {
    auto* ctx = static_cast<ArchiveClient*>(opaque);

    startPos &= ~AVSEEK_FORCE;

    if (startPos == AVSEEK_SIZE) return ctx->m_fileSize;

    int64_t target = 0;
    if (startPos == SEEK_SET) target = offset;
    else if (startPos == SEEK_CUR) target = ctx->m_position + offset;
    else if (startPos == SEEK_END) target = ctx->m_fileSize + offset;
    else return -1;

    target = std::clamp(target, 0LL, ctx->m_fileSize);
    if (target == ctx->m_position) return target;

    int64_t fastSeek = archive_seek_data(ctx->m_archive.get(), target, SEEK_SET);
    if (fastSeek >= 0) {
        ctx->m_position = fastSeek;
        return fastSeek;
    }

    if (target < ctx->m_position) {
        if (!ctx->GoToEntryAndSkip(target)) return -1;
    }
    else {
        std::vector<uint8_t> discard(64 * 1024);
        int64_t remaining = target - ctx->m_position;
        while (remaining > 0) {
            la_ssize_t got = archive_read_data(ctx->m_archive.get(), discard.data(), std::min(remaining, (int64_t)discard.size()));
            if (got <= 0) return -1;
            remaining -= got;
        }
        ctx->m_position = target;
    }

    return ctx->m_position;
}