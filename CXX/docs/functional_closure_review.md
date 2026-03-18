# HaruhiDB 功能闭环审查与 Catalog 持久化补充说明

## 1. 结论先行

当前 `CXX/` 这套实现已经不是纯内存原型，但也还不是完整数据库系统。

更准确地说，它当前最接近：

**半闭环单机内核**

原因很直接：

- 底层已经具备真实页文件、缓冲池、堆表、B+Tree、WAL 重放等持久化能力。
- 上层已经具备单进程内的表 CRUD、顺序扫描、过滤投影、手工索引查询能力。
- 但数据库对象目录 `Catalog` 还不持久化，重启后系统不知道有哪些表、schema 和索引，也没有统一启动恢复入口把这些对象重新组装出来。

如果只看底层页和结构体，系统已经能落盘。
如果从“数据库重启后还能自动发现并继续使用已有对象”这个标准看，系统还没有闭环。

## 2. 系统真实分层

当前实现的有效层次可以按下面这条链理解：

```text
DiskManager
  -> BufferPoolManager
    -> Page / TablePage / B+TreePage
      -> TableHeap / BPlusTree
        -> TableInfo / Catalog
          -> Executors
```

各层真正负责的事情如下：

| 层 | 主要对象 | 负责什么 | 是否直接持久化 | 是否负责恢复 |
| --- | --- | --- | --- | --- |
| 磁盘层 | [`DiskManager`](../src/include/storage/disk/disk_manager.h) | 管理数据库文件、页分配、free list、DBHeader | 是 | 是，恢复 `next_page_id` 和 `free_list_head` |
| 缓冲层 | [`BufferPoolManager`](../src/include/buffer/buffer_pool_manager/buffer_pool_manager.h) | 页缓存、脏页刷盘、替换 | 有条件，是通过显式刷盘或淘汰落盘 | 否，只是把页重新读入内存 |
| 页面层 | `Page` / `TablePage` / `BPlusTreePage` | 解释页内结构 | 页内容可持久化 | 否，自身不负责启动装配 |
| 结构层 | [`TableHeap`](../src/include/table/table_heap.h) / [`BPlusTree`](../src/include/storage/index/b_plus_tree.h) | 组织整张表和整棵树 | 是，但入口元数据不都持久化 | 半闭环，需外部提供入口页 |
| 对象层 | [`TableInfo`](../src/include/catalog/table_info.h) / [`Catalog`](../src/include/catalog/catalog.h) | 组织表名、schema、heap、index | `Catalog` 目前否 | 否 |
| 执行层 | Executors | 单进程内对表/索引做算子包装 | 依赖下层 | 否 |

最关键的边界是：

- `DiskManager` 负责“页文件是真实存在的”。
- `TableHeap` 和 `BPlusTree` 负责“页内容能按表/索引语义组织起来”。
- `Catalog` 本应负责“数据库里到底有哪些对象”，但这一层当前还只在内存里。

## 3. 关键状态归属

从“什么真的落盘、什么重启后还能回来”来看，当前关键状态可以分成四类。

| 状态 | 当前归属 | 状态分类 | 说明 |
| --- | --- | --- | --- |
| `DBHeader.magic/version/next_page_id/free_list_head` | `DiskManager` Page 0 | 已持久化 | 重启后可直接恢复 |
| 普通页字节内容 | 数据文件页 | 已持久化，但依赖刷盘 | 必须 `FlushPage/FlushAllPages`、页淘汰，或先写 WAL 后恢复 |
| WAL after-image | `.wal` 文件 | 已持久化 | `WalManager::Recover` 可重放到页 |
| 表页链中的 `next_page_id` | `TablePage` 页头 | 已持久化 | 知道首页后可重建整张表 |
| `TableHeap.first_page_id` | `TableHeap` 运行时成员 | 持久化能力存在，但入口未持久化 | 页链在磁盘上，但系统自己不知道首页是谁 |
| 索引 `root_page_id` | B+Tree header page | 已持久化 | 知道 `header_page_id` 后可恢复 |
| `BPlusTree.header_page_id` | `TableInfo`/外部调用方 | 持久化能力存在，但入口未持久化 | 索引页结构在磁盘上，但系统自己不知道 header page 是谁 |
| `table_oid` / `table_name` / `schema` | `Catalog` 内存映射 | 仅内存存在 | 进程结束即丢失 |
| `table -> index` 绑定关系 | `TableInfo.indexes_` | 仅内存存在 | 重启后无法自动重新挂接 |
| `ExecutorContext` / 执行器状态 | 进程内对象 | 仅内存存在 | 不参与恢复 |

把这张表换成一句话就是：

**页级状态大多已经能持久化，对象级状态还没有统一持久化。**

## 4. 以功能链路看当前闭环程度

### 4.1 表创建

链路：

```text
Catalog::CreateTable
  -> TableHeap::Create
    -> BufferPoolManager::NewPage(PageType::HEAP)
      -> DiskManager::AllocatePage
```

当前已经做到的部分：

- 能为表创建第一页。
- 首页会被初始化为合法 `TablePage`。
- 页号来自真实数据库文件，不是内存虚拟页号。

当前没有闭环的部分：

- `table_name`
- `table_oid`
- `schema`
- `first_page_id`

这些信息只登记在 `Catalog` / `TableInfo` 的内存对象里，重启后不会自动回来。

结论：

**“创建表”目前是进程内闭环，不是数据库级闭环。**

### 4.2 表插入 / 删除 / 更新

链路：

```text
InsertExecutor / DeleteExecutor / UpdateExecutor
  -> TableHeap
    -> TablePage
      -> Page dirty
        -> BufferPoolManager 刷盘
          -> DiskManager::WritePage
```

如果配置了 WAL，链路会变成：

```text
TableHeap 修改页
  -> 记录整页 after-image 到 WAL
  -> FlushLog
  -> 页后续刷盘，或下次启动时 Recover
```

当前已经做到的部分：

- 表行插入、逻辑删除、原位更新、迁移更新都能工作。
- `TableHeap` 在插入和页扩展时会维护页链。
- 配上 [`WalManager`](../src/include/storage/wal/wal_manager.h) 后，表页修改会先写 after-image WAL。

需要特别注意的现实约束：

- `BufferPoolManager` 析构不会自动 `FlushAllPages`。
- 所以“页面被修改过”不等于“数据一定已经进入 `.db` 文件”。
- 当前真正安全的两种路径只有：
  - 显式 `FlushPage/FlushAllPages`
  - 或者已经写入 WAL，并在重启时显式执行 `WalManager::Recover`

结论：

**表数据本身已经具备真实持久化能力，但恢复仍依赖外部知道 `first_page_id` 并手工重建 `TableHeap`。**

### 4.3 顺序扫描

链路：

```text
SeqScanExecutor
  -> TableHeap::Begin
    -> TableIterator
      -> BufferPoolManager::FetchPage
        -> TablePage / TupleCodec
```

当前已经做到的部分：

- 只扫描活跃 tuple。
- 能跨页顺序遍历整张表。
- 能正确跳过已删除记录。

这个功能的边界很清楚：

- 只要 `TableHeap` 已经被正确构造，顺序扫描就是闭环可用的。
- 但“谁来在重启后把 `TableHeap` 正确构造出来”这件事，当前系统自己还做不到。

结论：

**顺序扫描是对象级闭环能力，不是数据库级闭环能力。**

### 4.4 索引创建 / 查询 / 扫描

链路：

```text
Catalog::CreateIndex
  -> TableInfo::CreateIndex
    -> BPlusTree(BufferPoolManager*)
      -> 分配 index header page
```

当前已经做到的部分：

- `BPlusTree` 自己能创建 header page。
- header page 会持久化 `root_page_id`。
- 插入、删除、点查、范围扫描都能工作。
- 只要知道 `header_page_id`，重启后就能重新构造树对象。

当前没有闭环的部分：

- `Catalog::CreateIndex` 不会自动从表中回填历史数据。
- `InsertExecutor` / `DeleteExecutor` / `UpdateExecutor` 不会自动维护索引。
- 当前索引更像“可持久化 B+Tree 原语”，而不是“数据库级索引功能”。

执行层测试已经把这个边界说得很清楚：

- 需要手工 `BuildIndexFromTableRows` 才能把已有表数据灌进索引。
- `IndexScanExecutor` 会跳过 stale RID，但它并不会修复索引。

结论：

**索引结构本身可持久化、可恢复，但“索引作为表的一部分自动维护”还没有闭环。**

### 4.5 WAL 恢复

链路：

```text
DiskManager 打开数据库文件
  -> BufferPoolManager 建立页缓存
    -> WalManager::Recover
      -> 顺序扫描 WAL
      -> 把 after-image 覆盖回目标页
      -> FlushAllPages
      -> 截断 WAL
```

当前已经做到的部分：

- WAL 文件是真实文件。
- 恢复时会顺序重放 after-image。
- 恢复完成后会把脏页刷回数据库文件并截断 WAL。
- 测试已验证插入、删除、更新、页链接恢复可以靠 WAL 重放回来。

当前没有闭环的部分：

- 恢复是显式调用，不会自动在“数据库启动入口”里执行。
- WAL 只覆盖表页修改链路，不覆盖对象目录恢复。
- WAL 不保证索引一致性。

结论：

**WAL 本身已经是可用的恢复原语，但系统还没有完整的“启动时统一恢复”入口。**

### 4.6 重启后重新可用

这是当前最重要的一条链，也是最能区分“组件可持久化”和“数据库已闭环”的标准。

当前真实恢复链路是：

```text
1. 重启后重新打开 DiskManager
2. 创建 BufferPoolManager
3. 如有 WAL，手工调用 WalManager::Recover
4. 手工提供 first_page_id 重建 TableHeap
5. 手工提供 header_page_id 重建 BPlusTree
6. 手工重建 Catalog / TableInfo / schema 关系
7. 之后执行层才重新可用
```

问题就出在第 4 到第 6 步：

- 系统自己不知道 `first_page_id`
- 系统自己不知道有哪些表
- 系统自己不知道某表有哪些索引
- 系统自己不知道 schema 是什么

结论：

**当前系统没有数据库级自动恢复，只有模块级手工恢复。**

## 5. 能力矩阵

| 功能 | 当前状态 | 判断 |
| --- | --- | --- |
| 页分配、页读写、free list | 已闭环可用 | `DiskManager` 已是真实磁盘层 |
| 缓冲池读页、脏页刷盘 | 已闭环可用 | 但刷盘需要显式调用或依赖淘汰 |
| WAL 记录与 redo | 已闭环可用 | 但需显式 `Recover` |
| 堆表 tuple CRUD | 半闭环，可手工恢复 | 表页数据能持久化，入口页未自动发现 |
| 堆表顺序扫描 | 半闭环，可手工恢复 | 先要能重建 `TableHeap` |
| B+Tree 点查/范围扫 | 半闭环，可手工恢复 | 先要知道 `header_page_id` |
| 索引作为独立结构持久化 | 半闭环，可手工恢复 | 树结构可恢复，表绑定不可自动恢复 |
| `Catalog` 表目录 | 仅进程内可用 | 当前明确不持久化 |
| `schema` 自动恢复 | 仅进程内可用 | 只保存在 `TableInfo` |
| `table -> index` 自动恢复 | 仅进程内可用 | 绑定关系只存在内存 |
| 执行器 CRUD/过滤/投影 | 仅进程内可用 | 依赖已构造好的对象 |
| DML 自动维护索引 | 底层能力存在但上层未闭环 | 当前执行器只改表，不改索引 |

## 6. 测试证据基线

本审查不是只靠读代码得出的，下面这些测试构成了本次判断的证据基线：

| 测试 | 证明了什么 |
| --- | --- |
| `DiskManagerTest.PersistenceTest` | 页写入在 `Flush` 后会跨重启保留 |
| `DiskManagerTest.FreeListPersistenceLinkageTest` | free list 头信息会跨重启恢复 |
| `BufferPoolManagerTest.DataPersistsAcrossManagerRestart` | 缓冲池数据必须显式 `FlushAllPages` 后才能稳定跨重启读回 |
| `BPlusTreeTest.HeaderPagePersistsRootAndRecoversTree` | 知道 `header_page_id` 后可恢复 B+Tree |
| `WalManagerTest.AppendFlushAndRecoverRedoAppliesAfterImage` | WAL redo 能把整页 after-image 重放回数据库页 |
| `WalManagerTest.RecoverTruncatesWalFileOnSuccess` | 成功恢复后 WAL 会被截断 |
| `TableHeapWalTest.InsertThenRecover` | 只要知道 `first_page_id`，表数据可经 WAL 恢复 |
| `TableHeapWalTest.NewPageLinkingSurvivesRecover` | 表页链关系可经 WAL 恢复 |
| `TableHeapWalTest.RecoveryDoesNotGuaranteeIndexConsistency` | WAL 恢复当前不保证索引一致性 |
| `CatalogTableInfoTest.CatalogLoadIndexFromHeaderPageId` | `Catalog` 只能通过手工 `header_page_id` 重新挂索引 |
| `ExecutorFunctionalTest.InsertExecutorMixedValidInvalidInputLeavesPartialWrites` | 执行层没有事务回滚 |
| `ExecutorFunctionalTest.IndexScanSkipsStaleEntries` | 索引扫描会跳过 stale RID，但不自动修复索引 |

## 7. 当前系统的最终判断

一句话判断：

**当前 HaruhiDB 已经实现了“可持久化的页式存储 + 可恢复的堆表/B+Tree 原语 + 单进程执行层”，但还没有实现“数据库对象目录持久化与自动启动恢复”，因此它是半闭环单机内核，而不是最小完整数据库。**

这句话里的三层含义分别是：

- 已实现什么：
  - 真实数据文件
  - 缓冲池
  - 堆表
  - B+Tree
  - WAL redo
  - 单进程表和索引访问
- 哪些能力真正可用：
  - 单进程内表 CRUD
  - 顺序扫描
  - 手工建索引、手工回填、手工查询
  - 显式刷盘或显式 WAL 恢复
- 哪些仍未闭环：
  - `Catalog`
  - schema 自动恢复
  - 表和索引自动发现
  - 启动时统一恢复入口
  - DML 自动维护索引

## 8. 构成最小完整数据库还差什么

如果目标不是“大型数据库”，而只是把当前系统补成**最小完整数据库**，还差的关键环节其实不多。

### 8.1 Catalog 持久化

这是最关键的一项。

没有这一层，系统重启后就无法自动知道：

- 数据库里有哪些表
- 每张表的 `schema`
- 每张表的 `first_page_id`
- 每张表绑定了哪些索引
- 每个索引的 `header_page_id`

### 8.2 统一启动恢复入口

当前恢复是“先恢复页，再手工组对象”。

最小完整数据库需要一个统一入口，把下面顺序固化下来：

```text
Open DB file
-> create BufferPoolManager
-> recover WAL
-> load Catalog metadata
-> rebuild TableInfo
-> rebuild TableHeap/BPlusTree
-> expose Catalog to executors
```

### 8.3 表与索引的绑定和自动维护

当前索引不是表的自动一致性组成部分。

最小完整数据库至少要做到：

- `CreateIndex` 时可以登记到持久化 `Catalog`
- 插入/删除/更新表数据时能同步更新对应索引
- 重启后能自动把索引重新挂回表

### 8.4 明确的持久化约定

当前底层可以持久化，但系统级约定还没固化。

至少需要明确一种统一策略：

- 正常关闭时显式刷脏页
- 异常关闭时依赖 WAL 恢复

这样用户面对系统时，才会知道“什么情况下数据一定能回来”。

## 9. Catalog 持久化补充设计

这一节是补充说明，不代表当前已经实现。

### 9.1 设计目标

目标不是引入复杂系统表、自举 SQL 或完整 DDL 历史，而是在当前项目体量下，用最小复杂度把数据库补成真正可重启系统。

默认方案：

**单独的 Catalog 元数据页/页链**

而不是先做系统表。

### 9.2 最小持久化对象

最小只需要持久化两类对象：

```text
TableMeta = {
  table_oid,
  table_name,
  serialized_schema,
  first_page_id
}

IndexMeta = {
  index_oid,
  table_oid,
  index_name,
  header_page_id
}
```

这样就足以在重启后自动重建：

- `Catalog`
- `TableInfo`
- `TableHeap`
- `BPlusTree`

### 9.3 最小元数据入口

数据库级持久化元数据里需要新增：

```text
catalog_meta_page_id
```

它应该存放在数据库级持久化头信息里，而不是只放在进程内对象中。

当前最直接的做法是扩展 `DBHeader`，让数据库文件自己知道 Catalog 元数据从哪里开始。

### 9.4 最小恢复顺序

补上 `Catalog` 持久化后，最小恢复顺序应固定为：

```text
1. DiskManager 打开数据库文件
2. BufferPoolManager 建立缓存
3. WalManager::Recover 先恢复页状态
4. 加载 Catalog 元数据
5. 重建 TableInfo
6. 用 first_page_id 重建 TableHeap
7. 用 header_page_id 重建 BPlusTree
8. Catalog 恢复完成后，执行层才算真正可用
```

这个顺序里，最重要的原则是：

**先恢复页，再恢复对象。**

否则 `Catalog` 可能会拿到尚未 redo 的旧页状态。

### 9.5 为什么这一步最关键

一旦补上这条链，系统最大的变化不是“多了一个类”，而是：

**表和索引在重启后可以被系统自动重新发现。**

这正是当前系统从“半闭环单机内核”走向“最小完整数据库”的分水岭。

### 9.6 最小验收场景

如果后续真的实现这一补充设计，最小验收只需要覆盖下面四个场景：

1. 创建表并重启后自动重新发现该表。
2. `schema` 与 `first_page_id` 恢复正确，顺序扫描仍可用。
3. 索引 `header_page_id` 能自动恢复并重建索引对象。
4. WAL 恢复后再加载 `Catalog`，表和索引入口仍然一致。

---

如果只想继续做“个人可控复杂度”的数据库，这份文档后面的建议优先级很明确：

**先补 Catalog 持久化和统一启动恢复入口，再考虑更复杂的能力。**
