/**
 * CXX/src/storage/wal/wal_manager.cxx
 *
 * ========================= 实现目标 =========================
 *
 * 本文件实现 WalManager 的日志写入与恢复逻辑。
 *
 * 主要完成：
 *
 * 1. 打开或创建 WAL 文件
 * 2. 追加日志记录
 * 3. 刷新日志
 * 4. 启动恢复时顺序扫描 WAL
 * 5. 重放 after-image 到页面
 * 6. 恢复完成后截断 WAL
 * 7. 根据现有 WAL 恢复 next_lsn_
 *
 *
 * ========================= 核心流程 =========================
 *
 * AppendLog:
 *   检查记录合法性
 *   生成 DiskRecordHeader
 *   写 header
 *   写 after_image
 *
 * Recover:
 *   顺序读取 WAL 文件
 *   验证 header
 *   读 payload
 *   调用 RedoUnlocked
 *   FlushAllPages
 *   TruncateWalUnlocked
 *
 *
 * ========================= 当前实现说明 =========================
 *
 * 当前实现是最小可用 WAL：
 *
 * - 仅支持完整页 after-image
 * - 不区分 undo/redo
 * - 不做 checkpoint
 * - 恢复后直接截断整份 WAL
 */

#include "storage/wal/wal_manager.h"

#include "storage/page/page.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace HaruhiDB
{
namespace storage
{
namespace wal
{
    namespace
    {
        bool IsSupportedLogType(uint8_t raw_type)
        {
            return raw_type == static_cast<uint8_t>(LogRecordType::PUT) ||
                   raw_type == static_cast<uint8_t>(LogRecordType::DELETE);
        }
    } // namespace

    /**
     * @param wal_path WAL 文件路径
     */
    WalManager::WalManager(std::filesystem::path wal_path)
        : wal_path_(std::move(wal_path))
    {
        // step 1: 确保 WAL 所在目录存在。
        std::error_code ec;
        const auto parent = wal_path_.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }

        // step 2: 打开 WAL 追加文件，并恢复 next_lsn_。
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

    /**
     * @param record 待追加日志
     */
    bool WalManager::AppendLog(const LogRecord& record)
    {
        std::lock_guard<std::mutex> guard(mutex_);

        // step 1: 确保 WAL 文件可写，并校验记录基础字段。
        if (!EnsureAppendFileOpen()) {
            return false;
        }
        if (record.page_id == INVALID_PAGE_ID || record.page_id == 0) {
            return false;
        }
        if (record.payload_len != WAL_PAYLOAD_LEN) {
            return false;
        }

        // step 2: 组装磁盘记录头，并分配/推进 LSN。
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

        // step 3: 顺序写入 header 与整页 after-image。
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

    /**
     * @param bpm 缓冲池管理器
     */
    bool WalManager::Recover(buffer::BufferPoolManager* bpm)
    {
        // step 1: 检查恢复依赖，并确保当前追加文件状态正常。
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

        // step 2: 以只读方式顺序扫描 WAL 文件。
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

            // step 3: 校验记录头；遇到损坏或不完整记录则停止扫描。
            if (header.magic != WAL_MAGIC ||
                header.version != WAL_VERSION ||
                header.payload_len != WAL_PAYLOAD_LEN ||
                !IsSupportedLogType(header.type)) {
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

            // step 4: 对每条有效记录执行重放。
            if (!RedoUnlocked(record, bpm)) {
                return false;
            }
        }

        // step 5: 把重放后的脏页刷回磁盘。
        if (!bpm->FlushAllPages().has_value()) {
            return false;
        }

        // step 6: 恢复完成后截断 WAL，并重置 next_lsn_。
        if (!TruncateWalUnlocked()) {
            return false;
        }
        next_lsn_ = 1;
        return true;
    }

    /**
     * @param record 日志记录
     * @param bpm    缓冲池管理器
     */
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
        // step 1: 关闭当前追加文件句柄。
        if (append_file_.is_open()) {
            append_file_.flush();
            append_file_.close();
        }

        // step 2: 以 trunc 方式清空 WAL 文件。
        std::ofstream trunc_file(wal_path_, std::ios::binary | std::ios::trunc);
        if (!trunc_file.is_open()) {
            return false;
        }
        trunc_file.flush();
        if (!trunc_file) {
            return false;
        }
        trunc_file.close();

        // step 3: 重新打开追加句柄。
        return EnsureAppendFileOpen();
    }

    /**
     * @param record 日志记录
     * @param bpm    缓冲池管理器
     */
    bool WalManager::RedoUnlocked(const LogRecord& record, buffer::BufferPoolManager* bpm)
    {
        // step 1: 检查重放依赖与记录合法性。
        if (bpm == nullptr) {
            return false;
        }
        if (record.page_id == INVALID_PAGE_ID || record.page_id == 0) {
            return false;
        }
        if (record.payload_len != WAL_PAYLOAD_LEN) {
            return false;
        }

        // step 2: 把目标页取入缓冲池。
        auto page_exp = bpm->FetchPage(record.page_id);
        if (!page_exp.has_value()) {
            return false;
        }

        // step 3: 用 after-image 覆盖页面内容，并标记为脏页。
        Page* page = page_exp.value();
        page->WLock();
        page->Data() = record.after_image;
        page->MarkDirty();
        page->WUnLock();

        // step 4: 解除 pin，让后续统一刷盘。
        return bpm->UnpinPage(record.page_id, true);
    }

    void WalManager::UpdateNextLsnUnlocked()
    {
        // step 1: 默认从 1 开始。
        next_lsn_ = 1;

        // step 2: 顺序扫描现有 WAL，取最大 lsn + 1。
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

            if (!IsSupportedLogType(header.type)) {
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

} // namespace wal
} // namespace storage
} // namespace HaruhiDB
