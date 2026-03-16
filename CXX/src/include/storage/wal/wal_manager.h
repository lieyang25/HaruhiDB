/**
 * CXX/src/include/storage/wal/wal_manager.h
 *
 * ========================= 设计目标 =========================
 *
 * WalManager 负责管理数据库的预写日志（WAL）。
 *
 * 当前实现采用 after-image 日志形式，
 * 即把页面修改后的完整页镜像写入 WAL 文件，
 * 在恢复时再将这些镜像重放回数据页。
 *
 * 核心职责：
 *
 * 1. 追加日志记录
 * 2. 刷新日志到磁盘
 * 3. 启动恢复时重放日志
 * 4. 维护递增 LSN
 * 5. 恢复完成后截断 WAL
 *
 *
 * ========================= 为什么需要 WalManager =========================
 *
 * 当页面修改已经发生，
 * 但数据页还未来得及安全落盘时，
 * 系统崩溃会导致修改丢失。
 *
 * WAL 的作用是：
 *
 * - 先把修改写到日志
 * - 确保日志持久化
 * - 再允许页面写回磁盘
 *
 * 这样系统重启后就可以根据日志恢复页面状态。
 *
 *
 * ========================= WalManager 在系统中的位置 =========================
 *
 * TableHeap / BufferPoolManager
 *          │
 *          └── WalManager
 *                 └── wal file
 *
 *
 * ========================= 当前日志组织 =========================
 *
 *   +-------------------+
 *   | DiskRecordHeader  |
 *   +-------------------+
 *   | after_image page  |
 *   +-------------------+
 *   | DiskRecordHeader  |
 *   +-------------------+
 *   | after_image page  |
 *   +-------------------+
 *
 * 其中：
 *
 * - 每条日志固定携带一整页 after_image
 * - payload_len 当前固定为 PAGE_SIZE
 */

#pragma once

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "common/config.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <type_traits>

namespace HaruhiDB
{
namespace storage
{
namespace wal
{

    enum class LogRecordType : uint8_t {
        PUT = 1,
        DELETE = 2,
    };

    /**
     * 内存中的 WAL 记录。
     */
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

    class WalManager {
    public:
        /**
         * @param wal_path WAL 文件路径
         */
        explicit WalManager(std::filesystem::path wal_path);

        virtual ~WalManager();

        /**
         * 追加一条日志记录。
         *
         * @param record 待追加日志
         * @return 成功返回 true
         */
        virtual bool AppendLog(const LogRecord& record);

        /**
         * 刷新 WAL 文件缓冲区。
         *
         * @return 成功返回 true
         */
        virtual bool FlushLog();

        /**
         * 重放整个 WAL 文件并执行恢复。
         *
         * @param bpm 缓冲池管理器
         * @return 成功返回 true
         */
        bool Recover(buffer::BufferPoolManager* bpm);

        /**
         * 重放一条日志记录。
         *
         * @param record 日志记录
         * @param bpm    缓冲池管理器
         * @return 成功返回 true
         */
        bool Redo(const LogRecord& record, buffer::BufferPoolManager* bpm);

        /**
         * 返回 WAL 文件路径。
         */
        const std::filesystem::path& WalPath() const noexcept { return wal_path_; }

    private:
        /**
         * 磁盘上的 WAL 记录头。
         *
         * payload 不在该结构内部，
         * 紧随 header 之后顺序写入。
         */
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
        /**
         * 确保追加文件句柄已打开。
         */
        bool EnsureAppendFileOpen();

        /**
         * 截断 WAL 文件。
         *
         * @note 调用方需已持有 mutex_
         */
        bool TruncateWalUnlocked();

        /**
         * 在已持锁状态下重放一条日志。
         *
         * @note 调用方需已持有 mutex_
         */
        bool RedoUnlocked(const LogRecord& record, buffer::BufferPoolManager* bpm);

        /**
         * 重新扫描 WAL，恢复 next_lsn_。
         *
         * @note 调用方需已持有 mutex_
         */
        void UpdateNextLsnUnlocked();

        std::filesystem::path wal_path_;
        std::fstream append_file_;
        std::mutex mutex_;
        lsn_t next_lsn_{1};
    };

} // namespace wal
} // namespace storage
} // namespace HaruhiDB
