# HaruhiDB 系统架构总览

## 1. 当前系统定位

HaruhiDB 当前最适合被理解为：

**一个基于 C++23 的单机关系型数据库内核，实现了最小但完整的运行入口、对象目录、表存储、B+Tree 索引、缓冲管理和 redo 恢复主线。**

它已经不是纯内存原型，但也还不是完整数据库产品。当前明确成立的范围包括：

- 统一运行入口：`runtime::DatabaseRuntime::Open()`
- 执行器流水线：`Values / SeqScan / Filter / Projection / Insert / Delete / Update / IndexScan`
- 持久化目录：`Catalog` 会持久化表与索引元数据，并在重启后自动恢复
- 表数据组织：`TableHeap + TablePage + TupleCodec`
- 索引组织：整数键 `BPlusTree`
- 缓冲与页管理：`BufferPoolManager + LruKReplacer + DiskManager`
- 崩溃恢复：`WalManager` 基于整页 after-image 做 redo

当前还不在闭环范围内的内容包括：

- SQL parser / planner / optimizer
- 事务管理与并发控制
- undo logging / checkpoint
- 通用多列、多类型索引语义

## 2. 分层主线

从上到下，当前系统可以按下面这条主线理解：

```text
DatabaseRuntime
  -> ExecutorContext / Executors
    -> Catalog / TableInfo / Schema
      -> TableHeap / BPlusTree
        -> BufferPoolManager
          -> Page / TablePage / B+TreePage
            -> DiskManager / WalManager
```

更接近真实依赖关系的展开图如下：

```text
Client / Test code
        |
        v
DatabaseRuntime
  +-- DiskManager
  +-- BufferPoolManager
  +-- WalManager
  +-- Catalog
  `-- ExecutorContext

Catalog
  `-- TableInfo
       +-- Schema
       +-- TableHeap
       `-- BPlusTree(indexes...)

TableHeap / BPlusTree
  `-- BufferPoolManager
       +-- LruKReplacer
       `-- DiskManager

TableHeap mutations
  `-- WalManager
```

## 3. 各层职责边界

| 层 | 主要对象 | 负责什么 | 不负责什么 |
| --- | --- | --- | --- |
| Runtime | `DatabaseRuntime` | 装配系统对象、固定启动顺序、统一暴露运行时入口 | 不负责具体查询逻辑或页读写细节 |
| Execution | `AbstractExecutor` 及各子类 | 组织行流、执行扫描/过滤/投影/DML、协调表和索引操作 | 不负责页缓存与文件 I/O |
| Catalog | `Catalog` / `TableInfo` / `Schema` / `Column` | 管理数据库对象、名称/OID、表与索引元数据持久化、对象恢复 | 不负责 tuple 字节布局和页内操作 |
| Table / Record | `TableHeap` / `TableIterator` / `RID` / `Tuple` / `TupleCodec` | 组织表页链、记录定位、Value 与 Tuple 编解码 | 不负责查询计划和目录管理 |
| Index | `BPlusTree` / `IndexIterator` | 管理键到 RID 的索引结构和范围扫描 | 不负责 tuple 解码与上层行投影 |
| Buffer | `BufferPoolManager` / `LruKReplacer` | 页驻留、pin/unpin、替换、脏页刷盘 | 不负责 schema、表名或索引语义 |
| Storage / Recovery | `DiskManager` / `WalManager` / `Page` family | 数据文件页级持久化、redo 日志、页结构解释 | 不负责执行层流水线 |
| Type / Common | `TypeId` / `Value` / config constants | 值表示、类型系统、基础常量 | 不负责数据库对象组织 |

## 4. 核心对象关系

### 4.1 DatabaseRuntime

`DatabaseRuntime` 是当前系统最接近“数据库实例”的对象。

它持有：

- `DiskManager`
- `BufferPoolManager`
- `WalManager`
- `Catalog`
- `ExecutorContext`

它的职责不是实现细节，而是确保系统按正确顺序被装起来。

### 4.2 Catalog 与 TableInfo

`Catalog` 是对象目录层，负责回答：

- 有哪些表
- 表名对应哪个 `table_oid`
- `table_oid` 对应哪个 `TableInfo`
- 某张表有哪些索引

`TableInfo` 是一张表的运行时外壳，组织以下对象：

- 表身份：`oid`, `name`
- 表结构：`schema`
- 表数据入口：`table_heap`
- 索引入口：`indexes`

因此，执行层通常不是直接拿 `TableHeap` 或 `BPlusTree` 到处传，而是从 `TableInfo` 进入整张表。

### 4.3 TableHeap 与 BPlusTree

这是当前真正承接“数据入口”的两类对象：

- `TableHeap` 负责整张表的堆式组织
- `BPlusTree` 负责键到 RID 的索引路径

两者都不直接管理数据库对象命名，也不负责系统启动，只负责各自的物理访问语义。

### 4.4 BufferPoolManager

`BufferPoolManager` 是所有页访问的中间层。无论上层是：

- `TableHeap`
- `BPlusTree`
- `WalManager::Recover`

最终都要通过 `FetchPage / NewPage / UnpinPage / FlushPage` 这一套接口和磁盘页交互。

### 4.5 DiskManager 与 WalManager

这两个对象一起构成“持久化与恢复底座”：

- `DiskManager` 管理数据库文件、页分配、头页状态
- `WalManager` 管理 WAL 文件和 redo 恢复

其中：

- `.db` 文件保存长期页状态与 `DBHeader`
- `.wal` 文件保存尚未安全落入 `.db` 的页 after-image

## 5. 目前系统成立的几个关键约束

### 5.1 写路径的统一入口

对当前实现来说，最稳定的写路径是经过执行层：

- `InsertExecutor`
- `DeleteExecutor`
- `UpdateExecutor`

因为这里不仅会修改 `TableHeap`，还会同步协调索引维护。

### 5.2 索引语义是“当前实现约定”，不是通用规则

当前执行层索引维护建立在一个明确前提上：

- 索引键取 schema 的第 1 列
- 该列必须是 `INTEGER NOT NULL`

所以它更像“当前实现的固定索引模型”，而不是通用索引框架。

### 5.3 Catalog 是当前对象恢复的锚点

当前重启后能否重新发现表和索引，关键不在 `TableHeap` 或 `BPlusTree` 自身，而在：

- `DBHeader.catalog_meta_page_id`
- `Catalog` 能否从这个入口恢复 `TableInfo / TableHeap / BPlusTree`

换句话说，`Catalog` 现在已经是数据库对象层真正的持久化入口。

## 6. 建议的源码阅读顺序

如果要按依赖方向理解代码，推荐顺序如下：

1. `src/include/runtime/database_runtime.h`
2. `src/runtime/database_runtime.cxx`
3. `src/include/execution/` + `src/execution/`
4. `src/include/catalog/` + `src/catalog/catalog.cxx`
5. `src/include/table/` + `src/table/table_heap.cxx`
6. `src/include/storage/index/` + `src/storage/index/`
7. `src/include/buffer/` + `src/buffer/`
8. `src/include/storage/disk/` + `src/include/storage/wal/`
9. `src/include/type/` + `src/include/common/`

这个顺序最符合“运行入口向下压到存储底座”的理解路径。
