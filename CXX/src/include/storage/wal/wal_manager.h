#pragma once

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "common/config.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <type_traits>

namespace HaruhiDB::storage::wal
{

constexpr uint32_t WAL_MAGIC = 0x57414C31U;
constexpr uint32_t WAL_VERSION = 1U;
constexpr uint32_t WAL_PAYLOAD_LEN = static_cast<uint32_t>(PAGE_SIZE);

enum class LogRecordType : uint8_t {
    PUT = 1,
    DELETE = 2,
};

struct LogRecord
{
    uint32_t magic{WAL_MAGIC};
    uint32_t version{WAL_VERSION};
    LogRecordType type{LogRecordType::PUT};
    lsn_t lsn{0};
    page_id_t page_id{INVALID_PAGE_ID};
    uint32_t payload_len{WAL_PAYLOAD_LEN};
    page_data_t after_image{};
};

class WalManager
{
public:
    explicit WalManager(std::filesystem::path wal_path);
    ~WalManager();

    bool AppendLog(const LogRecord& record);
    bool FlushLog();

    bool Recover(buffer::BufferPoolManager* bpm);
    bool Redo(const LogRecord& record, buffer::BufferPoolManager* bpm);

    const std::filesystem::path& WalPath() const noexcept { return wal_path_; }

private:
    struct DiskRecordHeader
    {
        uint32_t magic{WAL_MAGIC};
        uint32_t version{WAL_VERSION};
        uint8_t type{0};
        uint8_t reserved[7]{};
        lsn_t lsn{0};
        page_id_t page_id{INVALID_PAGE_ID};
        uint32_t payload_len{0};
    };

    static_assert(std::is_trivially_copyable_v<DiskRecordHeader>);
    static_assert(sizeof(DiskRecordHeader) == 32);

private:
    bool EnsureAppendFileOpen();
    bool TruncateWalUnlocked();
    bool RedoUnlocked(const LogRecord& record, buffer::BufferPoolManager* bpm);
    void UpdateNextLsnUnlocked();

    std::filesystem::path wal_path_;
    std::fstream append_file_;
    std::mutex mutex_;
    lsn_t next_lsn_{1};
};

} // namespace HaruhiDB::storage::wal
