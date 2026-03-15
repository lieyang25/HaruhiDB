#include "storage/wal/wal_manager.h"

#include "storage/page/page.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace HaruhiDB::storage::wal
{

WalManager::WalManager(std::filesystem::path wal_path)
    : wal_path_(std::move(wal_path))
{
    std::error_code ec;
    const auto parent = wal_path_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }

    std::lock_guard<std::mutex> guard(mutex_);
    if (!EnsureAppendFileOpen()) {
        throw std::runtime_error("WalManager: failed to open wal file");
    }
    UpdateNextLsnUnlocked();
}

WalManager::~WalManager()
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (append_file_.is_open()) {
        append_file_.flush();
        append_file_.close();
    }
}

bool WalManager::AppendLog(const LogRecord& record)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (!EnsureAppendFileOpen()) {
        return false;
    }

    if (record.page_id == INVALID_PAGE_ID || record.page_id == 0) {
        return false;
    }

    if (record.payload_len != WAL_PAYLOAD_LEN) {
        return false;
    }

    DiskRecordHeader header{};
    header.magic = WAL_MAGIC;
    header.version = WAL_VERSION;
    header.type = static_cast<uint8_t>(record.type);
    header.lsn = record.lsn == 0 ? next_lsn_++ : record.lsn;
    header.page_id = record.page_id;
    header.payload_len = WAL_PAYLOAD_LEN;

    if (record.lsn >= next_lsn_) {
        next_lsn_ = record.lsn + 1;
    }

    append_file_.write(
        reinterpret_cast<const char*>(&header),
        static_cast<std::streamsize>(sizeof(DiskRecordHeader)));
    append_file_.write(
        reinterpret_cast<const char*>(record.after_image.data()),
        static_cast<std::streamsize>(WAL_PAYLOAD_LEN));
    return static_cast<bool>(append_file_);
}

bool WalManager::FlushLog()
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (!EnsureAppendFileOpen()) {
        return false;
    }

    append_file_.flush();
    return static_cast<bool>(append_file_);
}

bool WalManager::Recover(buffer::BufferPoolManager* bpm)
{
    if (bpm == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> guard(mutex_);
    if (!EnsureAppendFileOpen()) {
        return false;
    }
    append_file_.flush();
    if (!append_file_) {
        return false;
    }

    std::ifstream in(wal_path_, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    while (true) {
        DiskRecordHeader header{};
        in.read(
            reinterpret_cast<char*>(&header),
            static_cast<std::streamsize>(sizeof(DiskRecordHeader)));
        const auto header_bytes = in.gcount();
        if (header_bytes == 0) {
            break;
        }
        if (header_bytes != static_cast<std::streamsize>(sizeof(DiskRecordHeader))) {
            break;
        }

        if (header.magic != WAL_MAGIC ||
            header.version != WAL_VERSION ||
            header.payload_len != WAL_PAYLOAD_LEN ||
            (header.type != static_cast<uint8_t>(LogRecordType::PUT) &&
             header.type != static_cast<uint8_t>(LogRecordType::DELETE))) {
            break;
        }

        LogRecord record{};
        record.magic = header.magic;
        record.version = header.version;
        record.type = static_cast<LogRecordType>(header.type);
        record.lsn = header.lsn;
        record.page_id = header.page_id;
        record.payload_len = header.payload_len;

        in.read(
            reinterpret_cast<char*>(record.after_image.data()),
            static_cast<std::streamsize>(WAL_PAYLOAD_LEN));
        if (in.gcount() != static_cast<std::streamsize>(WAL_PAYLOAD_LEN)) {
            break;
        }

        if (!RedoUnlocked(record, bpm)) {
            return false;
        }
    }

    if (!bpm->FlushAllPages().has_value()) {
        return false;
    }

    if (!TruncateWalUnlocked()) {
        return false;
    }
    next_lsn_ = 1;
    return true;
}

bool WalManager::Redo(const LogRecord& record, buffer::BufferPoolManager* bpm)
{
    std::lock_guard<std::mutex> guard(mutex_);
    return RedoUnlocked(record, bpm);
}

bool WalManager::EnsureAppendFileOpen()
{
    if (append_file_.is_open()) {
        return true;
    }

    append_file_.open(wal_path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
    if (append_file_.is_open()) {
        return true;
    }

    append_file_.clear();
    std::ofstream create_file(wal_path_, std::ios::binary | std::ios::app);
    if (!create_file.is_open()) {
        return false;
    }
    create_file.close();

    append_file_.open(wal_path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
    return append_file_.is_open();
}

bool WalManager::TruncateWalUnlocked()
{
    if (append_file_.is_open()) {
        append_file_.flush();
        append_file_.close();
    }

    std::ofstream trunc_file(wal_path_, std::ios::binary | std::ios::trunc);
    if (!trunc_file.is_open()) {
        return false;
    }
    trunc_file.flush();
    if (!trunc_file) {
        return false;
    }
    trunc_file.close();

    return EnsureAppendFileOpen();
}

bool WalManager::RedoUnlocked(const LogRecord& record, buffer::BufferPoolManager* bpm)
{
    if (bpm == nullptr) {
        return false;
    }
    if (record.page_id == INVALID_PAGE_ID || record.page_id == 0) {
        return false;
    }
    if (record.payload_len != WAL_PAYLOAD_LEN) {
        return false;
    }

    auto page_exp = bpm->FetchPage(record.page_id);
    if (!page_exp.has_value()) {
        return false;
    }

    Page* page = page_exp.value();
    page->WLock();
    page->Data() = record.after_image;
    page->MarkDirty();
    page->WUnLock();
    return bpm->UnpinPage(record.page_id, true);
}

void WalManager::UpdateNextLsnUnlocked()
{
    next_lsn_ = 1;

    std::ifstream in(wal_path_, std::ios::binary);
    if (!in.is_open()) {
        return;
    }

    while (true) {
        DiskRecordHeader header{};
        in.read(
            reinterpret_cast<char*>(&header),
            static_cast<std::streamsize>(sizeof(DiskRecordHeader)));
        if (in.gcount() == 0) {
            break;
        }
        if (in.gcount() != static_cast<std::streamsize>(sizeof(DiskRecordHeader))) {
            break;
        }

        if (header.magic != WAL_MAGIC ||
            header.version != WAL_VERSION ||
            header.payload_len != WAL_PAYLOAD_LEN) {
            break;
        }

        if (header.type != static_cast<uint8_t>(LogRecordType::PUT) &&
            header.type != static_cast<uint8_t>(LogRecordType::DELETE)) {
            break;
        }

        page_data_t payload{};
        in.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(WAL_PAYLOAD_LEN));
        if (in.gcount() != static_cast<std::streamsize>(WAL_PAYLOAD_LEN)) {
            break;
        }

        next_lsn_ = std::max(next_lsn_, header.lsn + 1);
    }
}

} // namespace HaruhiDB::storage::wal
