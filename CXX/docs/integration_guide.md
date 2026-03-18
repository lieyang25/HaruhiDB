# HaruhiDB 当前实现使用方法

这份文档不是讲内部存储细节，而是面向下一步的“数据库外层”整理当前实现的可用接入面。

如果想直接看可运行代码，可以配合阅读 `CXX/example/quickstart.cxx`。

这里的“外层”可以是：

- 一个更高层的 `Database` 门面对象
- 命令行 / REPL
- 未来的 SQL 解析与执行调度层
- 演示代码、样例程序、测试辅助层

核心目标只有一个：

**把当前内核已经稳定下来的使用方式整理清楚，作为下一步外层封装的基础。**

## 1. 外层应该依赖哪些对象

对当前实现来说，外层最推荐直接依赖的对象只有这几类：

- `runtime::DatabaseRuntime`
- `catalog::Catalog`
- `catalog::TableInfo`
- `execution::ExecutorContext`
- `execution::*Executor`

如果只是做正常业务操作，外层通常不应该直接以这些对象为主入口：

- `table::TableHeap`
- `buffer::BufferPoolManager`
- `storage::DiskManager`
- `storage::wal::WalManager`

原因很简单：

- `DatabaseRuntime` 已经负责统一启动、恢复和装配
- `Catalog` 已经负责数据库对象查找
- `TableInfo` 已经提供一张表的完整运行时视图
- 执行器已经是当前最稳定的读写协调路径

如果外层大量直接操作 `TableHeap` 或 `BufferPoolManager`，就会很快绕开当前已经成型的对象边界。

## 2. 当前最推荐的启动方式

当前系统最推荐从 `DatabaseRuntime::Open()` 进入，而不是手工拼装底层对象。

```cpp
#include "runtime/database_runtime.h"

using namespace HaruhiDB;

auto open_exp = runtime::DatabaseRuntime::Open(
    db_path,
    runtime::DatabaseOpenOptions{
        .buffer_pool_size = 64,
        .lru_k = 2,
        .enable_wal = true,
        .wal_path = wal_path,
    });

if (!open_exp.has_value()) {
    throw std::runtime_error(open_exp.error());
}

auto runtime = std::move(open_exp.value());
auto* catalog = runtime.GetCatalog();
auto* exec_ctx = runtime.GetExecutorContext();
```

对外层来说，这一步的意义是：

- 不需要自己管理 `DiskManager -> BufferPoolManager -> WalManager -> Catalog` 的装配顺序
- 启动时会先做 WAL 恢复，再加载 Catalog
- 拿到 `catalog` 和 `exec_ctx` 后就可以直接开始对象操作和执行器调度

如果下一步要做更高层的 `DatabaseFacade`，它最自然的内部成员就应该是 `DatabaseRuntime`。

## 3. 建表的当前用法

当前建表分两步：

1. 用 `Schema::Create()` 构造 schema
2. 用 `Catalog::CreateTable()` 创建表

```cpp
#include "catalog/column.h"
#include "catalog/schema.h"

using namespace HaruhiDB;

auto schema_exp = catalog::Schema::Create({
    catalog::Column("id", type::TypeId::INTEGER, false),
    catalog::Column("name", type::TypeId::VARCHAR, 32, false),
});
if (!schema_exp.has_value()) {
    throw std::runtime_error(schema_exp.error());
}

auto table_exp = catalog->CreateTable("student", schema_exp.value());
if (!table_exp.has_value()) {
    throw std::runtime_error(table_exp.error());
}

catalog::TableInfo* table_info = table_exp.value();
```

外层在这里最适合做的事情是：

- 负责把更高层的列定义转换成 `Column` / `Schema`
- 用表名驱动对象创建
- 把返回的 `TableInfo*` 作为后续读写入口

## 4. 建索引的当前用法

如果需要索引，当前做法是通过 `Catalog::CreateIndex()` 创建：

```cpp
auto index_exp = catalog->CreateIndex(table_info->Oid(), "idx_student_id");
if (!index_exp.has_value()) {
    throw std::runtime_error(index_exp.error());
}

storage::BPlusTree* index = index_exp.value();
```

当前外层必须明确知道的约束是：

- 当前索引语义依赖 schema 的第 1 列
- 该列必须是 `INTEGER NOT NULL`
- 所以它更像“当前固定主键式索引模型”，不是任意索引接口

这意味着如果下一步要做数据库外层，比较合适的做法是：

- 不要先暴露“任意列建任意索引”的接口
- 先把外层索引能力收敛成当前实现真正支持的那一类

## 5. 插入数据的当前用法

当前插入最推荐走执行器路径：

- `ValuesExecutor` 负责提供输入行
- `InsertExecutor` 负责写表并维护索引

```cpp
#include "execution/insert_executor.h"
#include "execution/values_executor.h"

using namespace HaruhiDB;

auto values = std::make_unique<execution::ValuesExecutor>(
    exec_ctx,
    std::vector<std::vector<type::Value>>{
        {type::Value::Int32(1), type::Value::VarChar("haruhi")},
        {type::Value::Int32(2), type::Value::VarChar("mio")},
    });

execution::InsertExecutor insert(exec_ctx, table_info, std::move(values));
insert.Init();

execution::ExecutorRow result;
if (!insert.Next(&result)) {
    throw std::runtime_error("insert failed");
}

const int32_t* inserted_count = result.values[0].TryAs<int32_t>();
```

当前外层需要记住两点：

- `InsertExecutor` 的结果行不是业务数据，而是“成功插入了多少行”
- 如果表上已经挂接索引，标准插入路径会自动维护索引

所以从外层角度看，`InsertRows(table_name, rows)` 这类门面接口是很好建立的。

## 6. 顺序扫描的当前用法

当前顺序读表最直接的方式是 `SeqScanExecutor`：

```cpp
#include "execution/seq_scan_executor.h"

execution::SeqScanExecutor scan(exec_ctx, table_info);
scan.Init();

execution::ExecutorRow row;
while (scan.Next(&row)) {
    // row.values 是当前行值
    // row.rid / row.has_rid 描述物理位置
}
```

这条路径适合外层封装成：

- `ScanTable(table_name)`
- `ReadAll(table_name)`
- `ForEachRow(table_name, callback)`

如果下一步要加更高层查询接口，这条路径就是最自然的无优化基线实现。

## 7. 过滤与投影的当前用法

当前执行层还没有 SQL planner，所以条件和投影需要手工组合执行器：

- `SeqScanExecutor`
- `FilterExecutor`
- `ProjectionExecutor`

```cpp
auto scan_child = std::make_unique<execution::SeqScanExecutor>(exec_ctx, table_info);
auto filter = std::make_unique<execution::FilterExecutor>(
    exec_ctx,
    std::move(scan_child),
    [](const execution::ExecutorRow& row) {
        return !row.values.empty() && row.values[0] == type::Value::Int32(1);
    });

execution::ProjectionExecutor projection(exec_ctx, std::move(filter), {1});
projection.Init();
```

这说明当前外层如果要做最小查询层，最适合做的不是“写复杂优化器”，而是先做：

- 查询请求到执行器树的转换
- 谓词与列投影的简单装配

## 8. 索引扫描的当前用法

当前索引扫描使用 `IndexScanExecutor`：

```cpp
#include "execution/index_scan_executor.h"

execution::IndexScanExecutor index_scan(exec_ctx, table_info, index, 20);
index_scan.Init();

execution::ExecutorRow row;
while (index_scan.Next(&row)) {
    // row.values 是回表后的完整行
}
```

当前这个接口支持两种模式：

- 传入 `table_info`：返回回表后的完整行
- 不传 `table_info`：退化成只基于索引返回 key / RID 的扫描语义

从外层封装角度看，最值得优先暴露的是第一种，因为它更接近数据库用户真正想要的“按索引读行”。

## 9. 更新与删除的当前用法

当前更新和删除都建立在“子执行器提供 RID”这个前提上，所以最常见的组合是：

- `SeqScanExecutor`
- `FilterExecutor`
- `UpdateExecutor` 或 `DeleteExecutor`

### 9.1 删除

```cpp
auto scan_child = std::make_unique<execution::SeqScanExecutor>(exec_ctx, table_info);
auto filter = std::make_unique<execution::FilterExecutor>(
    exec_ctx,
    std::move(scan_child),
    predicate);

execution::DeleteExecutor deleter(exec_ctx, table_info, std::move(filter));
deleter.Init();
```

### 9.2 更新

```cpp
auto scan_child = std::make_unique<execution::SeqScanExecutor>(exec_ctx, table_info);
auto filter = std::make_unique<execution::FilterExecutor>(
    exec_ctx,
    std::move(scan_child),
    predicate);

execution::UpdateExecutor updater(
    exec_ctx,
    table_info,
    std::move(filter),
    [](const execution::ExecutorRow& row) {
        auto out = row.values;
        out[1] = type::Value::VarChar("updated");
        return out;
    });
updater.Init();
```

当前外层需要知道的限制是：

- 更新路径不允许修改当前索引键本身
- 删除和更新都应该尽量经过执行器，而不是直接调 `TableHeap`

## 10. 持久化与生命周期上的当前用法

如果外层要做一个“数据库服务对象”，生命周期上最重要的是两件事：

### 10.1 打开数据库

统一走：

- `DatabaseRuntime::Open()`

### 10.2 在需要稳定持久化的时间点显式刷盘

当前最直接的做法是：

```cpp
auto* bpm = runtime.GetBufferPoolManager();
if (bpm == nullptr || !bpm->FlushAllPages().has_value()) {
    throw std::runtime_error("flush failed");
}
```

虽然开启 WAL 后，重启时可以通过 `DatabaseRuntime::Open()` 自动恢复，但如果外层需要一个更明确的“保存”语义，显式 `FlushAllPages()` 仍然是最清楚的边界。

因此，下一步外层很适合直接提供一个：

- `Flush()`
- `CheckpointLikeSave()`
- `Close()` 中的显式刷盘步骤

## 11. 下一步外层最适合封装成什么

基于当前实现，最适合新增的不是再往下拆，而是往上做一个薄门面。

一个比较自然的外层接口大致会长这样：

```text
DatabaseFacade
  -> Open(path, options)
  -> CreateTable(name, schema_desc)
  -> CreatePrimaryIndex(table_name, index_name)
  -> InsertRows(table_name, rows)
  -> ScanTable(table_name)
  -> ScanByIndex(table_name, start_key)
  -> UpdateWhere(table_name, predicate, updater)
  -> DeleteWhere(table_name, predicate)
  -> Flush()
```

这层门面的职责不是改造内核，而是把当前已经稳定的内核对象重新收束成一组更容易被上层调用的接口。

## 12. 外层现在不应该急着做什么

为了让下一步整理更稳，当前外层不建议一开始就做这些事情：

- 先做复杂 SQL parser
- 先暴露任意列任意类型索引
- 先绕过执行器直接操作 `TableHeap`
- 先引入事务语义假象

更适合的顺序是：

1. 先把 `DatabaseRuntime + Catalog + Executor` 包成一层可复用门面
2. 先让常见 CRUD 和索引扫描有稳定调用方式
3. 再决定是否往 SQL 或更复杂查询接口扩展

## 13. 一句话总结当前使用面

如果只从“下一步怎么做数据库外层”这个角度看，当前实现最稳定的使用面就是：

- 用 `DatabaseRuntime` 打开数据库
- 用 `Catalog` 找到表对象
- 用 `TableInfo` 进入具体表
- 用执行器树完成读写
- 用 `FlushAllPages()` 在明确边界上刷盘

这五步已经足够支撑下一轮外层梳理了。
