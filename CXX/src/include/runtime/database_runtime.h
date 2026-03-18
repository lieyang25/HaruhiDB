#pragma once

#include "buffer/buffer_pool_manager/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "execution/executor_context.h"
#include "storage/disk/disk_manager.h"
#include "storage/wal/wal_manager.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace HaruhiDB
{
namespace runtime
{

struct DatabaseOpenOptions
{
    size_t buffer_pool_size{64};
    size_t lru_k{2};
    bool enable_wal{true};
    std::optional<std::filesystem::path> wal_path;
};

class DatabaseRuntime
{
public:
    static std::expected<DatabaseRuntime, std::string> Open(
        const std::filesystem::path& db_path,
        DatabaseOpenOptions options = {});

    DatabaseRuntime() = default;
    ~DatabaseRuntime() = default;

    DatabaseRuntime(const DatabaseRuntime&) = delete;
    DatabaseRuntime& operator=(const DatabaseRuntime&) = delete;
    DatabaseRuntime(DatabaseRuntime&&) noexcept = default;
    DatabaseRuntime& operator=(DatabaseRuntime&&) noexcept = default;

    catalog::Catalog* GetCatalog() const noexcept { return catalog_.get(); }
    execution::ExecutorContext* GetExecutorContext() const noexcept { return executor_context_.get(); }
    buffer::BufferPoolManager* GetBufferPoolManager() const noexcept { return buffer_pool_manager_.get(); }
    storage::DiskManager* GetDiskManager() const noexcept { return disk_manager_.get(); }
    storage::wal::WalManager* GetWalManager() const noexcept { return wal_manager_.get(); }
    const std::filesystem::path& DbPath() const noexcept { return db_path_; }

private:
    std::filesystem::path db_path_;
    std::unique_ptr<storage::DiskManager> disk_manager_;
    std::unique_ptr<buffer::BufferPoolManager> buffer_pool_manager_;
    std::unique_ptr<storage::wal::WalManager> wal_manager_;
    std::unique_ptr<catalog::Catalog> catalog_;
    std::unique_ptr<execution::ExecutorContext> executor_context_;
};

} // namespace runtime
} // namespace HaruhiDB
