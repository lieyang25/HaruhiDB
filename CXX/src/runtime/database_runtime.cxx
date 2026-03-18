#include "runtime/database_runtime.h"

#include <exception>
#include <utility>

namespace HaruhiDB
{
namespace runtime
{
namespace
{

std::filesystem::path BuildDefaultWalPath(const std::filesystem::path& db_path)
{
    auto wal_path = db_path;
    wal_path.replace_extension(".wal");
    return wal_path;
}

} // namespace

std::expected<DatabaseRuntime, std::string> DatabaseRuntime::Open(
    const std::filesystem::path& db_path,
    DatabaseOpenOptions options)
{
    if (options.buffer_pool_size == 0) {
        return std::unexpected("DatabaseRuntime::Open: buffer_pool_size must be greater than 0");
    }
    if (options.lru_k == 0) {
        return std::unexpected("DatabaseRuntime::Open: lru_k must be greater than 0");
    }

    DatabaseRuntime runtime;
    runtime.db_path_ = db_path;

    try {
        runtime.disk_manager_ = std::make_unique<storage::DiskManager>(db_path);
    } catch (const std::exception& e) {
        return std::unexpected("DatabaseRuntime::Open: open disk manager failed: " + std::string(e.what()));
    }

    try {
        runtime.buffer_pool_manager_ = std::make_unique<buffer::BufferPoolManager>(
            options.buffer_pool_size,
            runtime.disk_manager_.get(),
            options.lru_k);
    } catch (const std::exception& e) {
        return std::unexpected("DatabaseRuntime::Open: create buffer pool manager failed: " + std::string(e.what()));
    }

    if (options.enable_wal) {
        const auto wal_path = options.wal_path.value_or(BuildDefaultWalPath(db_path));
        try {
            runtime.wal_manager_ = std::make_unique<storage::wal::WalManager>(wal_path);
        } catch (const std::exception& e) {
            return std::unexpected("DatabaseRuntime::Open: open wal manager failed: " + std::string(e.what()));
        }

        if (!runtime.wal_manager_->Recover(runtime.buffer_pool_manager_.get())) {
            return std::unexpected("DatabaseRuntime::Open: wal recover failed");
        }
    }

    try {
        runtime.catalog_ = std::make_unique<catalog::Catalog>(runtime.buffer_pool_manager_.get());
    } catch (const std::exception& e) {
        return std::unexpected("DatabaseRuntime::Open: load catalog failed: " + std::string(e.what()));
    }

    if (runtime.wal_manager_ != nullptr) {
        runtime.catalog_->BindWalManager(runtime.wal_manager_.get());
    }

    runtime.executor_context_ = std::make_unique<execution::ExecutorContext>(runtime.catalog_.get());
    return runtime;
}

} // namespace runtime
} // namespace HaruhiDB
