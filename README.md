# HaruhiDB 架构总览

这份 README 不再把项目写成一份平铺的 API 名录，而是把当前系统重新组织成一条自上而下的主线：

`运行入口 -> 执行层 -> 目录层 -> 表与记录层 -> 索引层 -> 缓冲与存储层 -> WAL / 磁盘层`

目标只有一个：先恢复对整体结构的控制力，再继续迭代功能。

## 文档入口

如果你只想找“该看哪篇文档”，先看总索引：

- [HaruhiDB 文档总入口](docs/README.md)

按主题分组如下：

### 1) 快速上手与运行

- [Go 使用指南（Web + 网络 + Ollama）](docs/go-usage-guide.md)：单入口 serve、网页/API 路径、配置与启动
- [网页快速上手](docs/web-quickstart.md)：一键脚本启动 + 浏览器 UI
- [Web 原理与信息流](docs/web-principle-and-dataflow.md)：网页架构、信息流、输出语义
- [静态库与远程联动指南](docs/static-link-web-cli-and-remote-exec-guide.md)：Linux/macOS 静态库方案 + Web/serve 使用 + 远程联动
- [Go 启动配置示例](docs/configs/README.md)：`--config` 与现成配置文件
- [Go 配置参数总表](docs/config-parameter-reference.md)：所有配置键与 CLI 参数映射
- [Linux 一键网页启动脚本](scripts/start_web_one_click.sh)：自动启动服务并打开网页

### 2) 协议与模型集成

- [Action 协议 v1](docs/action-protocol-v1.md)：字段约束、动作列表、batch 规则
- [Go API 与动作覆盖矩阵](docs/go-api-action-capability-matrix.md)：完整 Go 能力 vs v1 暴露范围
- [Action v1 功能演示](docs/action-v1-showcase-example.md)：请求/响应示例集合
- [Action v2 功能演示](docs/action-v2-showcase-example.md)：含 create/drop table/index 的 v2 动作集示意
- [Action v1 请求模板（JSON）](docs/action-v1-request-template.json)：最小请求模板
- [Action v1 模型规范（JSON）](docs/action-v1-model-spec.json)：动作与参数的机器可读规范

### 3) 代码阅读与架构

- [Go 阅读指南（从最简单路径开始）](GO_READING_GUIDE.md)：Go 链路阅读顺序
- [CXX 文档索引](CXX/docs/README.md)：C++ 架构文档总入口
- [系统架构总览](CXX/docs/architecture_overview.md)：模块分层与职责
- [运行与数据链路](CXX/docs/execution_and_dataflow.md)：从入口到执行路径
- [持久化与恢复](CXX/docs/persistence_and_recovery.md)：WAL 与恢复流程
- [数据库文件与 WAL 布局](CXX/docs/file_layout.md)：磁盘布局细节
- [当前实现使用方法](CXX/docs/integration_guide.md)：集成方式与边界
- [可运行示例](CXX/example/README.md)：可直接执行的示例

## 项目当前定位

HaruhiDB 目前可以理解为一个正在持续收敛中的 C++23 单机关系型数据库内核，已经具备以下主干能力：

- 统一运行入口：`runtime::DatabaseRuntime::Open()`
- 执行器流水线：`Values / SeqScan / Filter / Projection / Insert / Delete / Update / IndexScan`
- 最小持久化目录：`Catalog` 会把表与索引元数据持久化，并在重启后自动恢复
- 表存储主线：`TableHeap + TablePage + TupleCodec`
- 索引主线：整数键 `BPlusTree`，支持恢复、扫描、回填与执行层维护
- 缓冲与页管理：`BufferPoolManager + LRU-K + DiskManager`
- 崩溃恢复主线：`WalManager` 基于整页 after-image 做 redo 恢复

同时也要明确当前边界：

- 还没有 SQL parser / optimizer / planner
- 执行计划目前由上层代码手工拼装执行器
- 当前索引语义聚焦于“首列为 `INTEGER NOT NULL` 的键”
- 事务、并发控制、checkpoint、undo 日志还不在当前闭环范围内

## 一条主线看全系统

```text
Client / Test / Demo code
        |
        v
runtime::DatabaseRuntime::Open(db_path, options)
        |
        +-- DiskManager
        +-- BufferPoolManager
        +-- WalManager::Recover()        [可选]
        +-- Catalog                      [装载表/索引元数据]
        +-- Catalog::BindWalManager()
        `-- ExecutorContext

Executor tree
        |
        +-- SeqScanExecutor ------------> TableInfo -> TableHeap -> BufferPoolManager
        +-- IndexScanExecutor ----------> TableInfo -> BPlusTree -> BufferPoolManager
        +-- Insert/Delete/Update -------> TableHeap + BPlusTree(index maintenance)
        `-- Filter/Projection/Values --> 纯执行层行流转

BufferPoolManager
        |
        +-- LruKReplacer
        `-- DiskManager <-> database file

TableHeap / page mutations
        |
        `-- WalManager (append after-image, flush log, redo on restart)
```

如果只抓住一件事，那就是：

- `DatabaseRuntime` 负责把系统装起来
- `Executor` 负责把“逻辑操作”往下压
- `Catalog` 负责把“数据库对象”组织起来
- `TableHeap / BPlusTree` 负责真正的数据入口
- `BufferPoolManager` 负责把页留在内存里并最终写回磁盘
- `WalManager` 负责在页落盘前先把恢复所需的信息写下来

## 启动顺序

当前系统最重要的入口是 [`CXX/src/include/runtime/database_runtime.h`](CXX/src/include/runtime/database_runtime.h) 中的 `DatabaseRuntime::Open()`，它把原本分散的启动动作收束成固定顺序：

1. 打开 `DiskManager`
   - 负责数据库文件、`DBHeader`、`next_page_id`、`free_list_head`、`catalog_meta_page_id`
2. 创建 `BufferPoolManager`
   - 建立页缓存、`page_table_`、空闲 frame 与 LRU-K 替换器
3. 如果启用 WAL，创建 `WalManager` 并执行 `Recover()`
   - 先做 redo 恢复，再把恢复后的脏页统一刷盘，并截断 WAL
4. 创建 `Catalog`
   - 从 `DBHeader.catalog_meta_page_id` 装载表和索引元数据
   - 重建 `TableInfo -> TableHeap -> BPlusTree` 这条对象链
5. 将 `WalManager` 绑定回 `Catalog` 里已恢复和后续新建的表对象
6. 创建 `ExecutorContext`
   - 作为执行器树的共享上下文入口

这条顺序很关键，因为它回答了“系统从哪里开始”和“各层初始化依赖谁”。

## 分层梳理

| 层次 | 负责什么 | 依赖什么 | 对外提供什么 |
| --- | --- | --- | --- |
| Runtime | 统一装配运行时对象，固定启动与恢复顺序 | DiskManager, BufferPoolManager, WalManager, Catalog | `Catalog*`, `ExecutorContext*`, `BufferPoolManager*`, `DiskManager*`, `WalManager*` |
| Execution | 组织行级执行流水线，承接扫描、过滤、投影和 DML | ExecutorContext, TableInfo, BPlusTree, TupleCodec | `ExecutorRow` 流、行数统计、DML 调用 |
| Catalog | 管理表/索引对象及其持久化元数据 | BufferPoolManager, DiskManager, TableHeap, BPlusTree | `CreateTable`, `GetTable`, `CreateIndex`, `GetIndex`, `GetAllTables` |
| Table / Record | 组织表页链、RID、Tuple 编解码与跨页读写 | BufferPoolManager, TablePage, WalManager | `InsertTuple`, `GetTuple`, `DeleteTuple`, `UpdateTuple`, `TableIterator` |
| Index | 维护键到 RID 的组织、恢复和范围扫描 | BufferPoolManager, B+Tree pages | `Insert`, `Remove`, `GetValue`, `Begin`, `IndexIterator` |
| Buffer | 管理页驻留、替换、pin/unpin、刷盘 | DiskManager, LruKReplacer | `FetchPage`, `NewPage`, `FlushPage`, `FlushAllPages`, `DeletePage` |
| Storage / WAL | 处理数据库文件页 I/O、DBHeader、redo 日志 | filesystem | `ReadPage`, `WritePage`, `AllocatePage`, `Recover`, `FlushLog` |
| Type / Common | 统一值类型、列类型、常量与错误码 | 基础库 | `TypeId`, `Value`, `PAGE_SIZE`, `page_id_t` 等 |

### 1. Runtime：系统真正的入口层

关键文件：

- `CXX/src/include/runtime/database_runtime.h`
- `CXX/src/runtime/database_runtime.cxx`

这一层不负责具体的数据读写，也不负责执行算子逻辑。它的职责只有两个：

- 统一把数据库运行时所需对象按正确顺序装配起来
- 给上层提供一个“开箱可用”的系统入口，避免调用方自己拼接 `DiskManager -> BufferPoolManager -> Catalog -> ExecutorContext`

可以把它看成当前项目中最接近“Database 实例”的对象。

### 2. Execution：把逻辑操作压成一条执行链

关键文件：

- `CXX/src/include/execution/executor.h`
- `CXX/src/include/execution/executor_context.h`
- `CXX/src/include/execution/*.h`
- `CXX/src/execution/*.cxx`

执行层当前采用非常清晰的 iterator/pipeline 风格：

- `AbstractExecutor` 定义 `Init()` / `Next()` 协议
- `ExecutorRow` 是执行层统一的中间行表示，附带可选 `RID`
- `ExecutorContext` 目前主要向执行器提供 `Catalog`

当前执行器可以按职责分为三类：

#### 2.1 数据源执行器

- `ValuesExecutor`：从内存里的值列表产生行
- `SeqScanExecutor`：通过 `TableHeap::Begin()/End()` 顺序扫描表
- `IndexScanExecutor`：从 `BPlusTree` 扫描键，再按 `RID` 回表取 tuple

#### 2.2 行变换执行器

- `FilterExecutor`：保留满足谓词的行
- `ProjectionExecutor`：裁剪或重排列

#### 2.3 数据修改执行器

- `InsertExecutor`
- `DeleteExecutor`
- `UpdateExecutor`

这三者的共同特点是：

- 输入是子执行器产出的 `ExecutorRow`
- 真实写入通过 `TableInfo -> TableHeap` 完成
- 如果表上已挂接索引，会通过 `execution/index_maintenance.*` 自动维护索引

当前这条 DML 主线的语义边界是：

- 索引键取表的第 1 列
- 该列必须是 `INTEGER NOT NULL`
- `CreateIndex()` 会对已有表数据做回填
- `Insert/Delete/Update` 经过执行层时会同步维护索引
- `UpdateExecutor` 当前不允许修改索引键本身，只允许在键不变前提下更新其他列或迁移 RID

这意味着现在的“索引一致性闭环”已经主要收敛在执行层路径内；如果直接绕过执行层手工操作 `TableHeap`，就需要调用方自己承担索引一致性。

### 3. Catalog：数据库对象的组织层

关键文件：

- `CXX/src/include/catalog/catalog.h`
- `CXX/src/include/catalog/table_info.h`
- `CXX/src/include/catalog/schema.h`
- `CXX/src/include/catalog/column.h`
- `CXX/src/catalog/catalog.cxx`

这一层解决的是“数据库里到底有哪些对象，它们如何被找到”的问题。

#### 3.1 Catalog 的职责

`Catalog` 当前负责：

- 创建表
- 用表名或 `table_oid` 查表
- 枚举所有表
- 创建索引 / 加载已有索引
- 分配连续的 `table_oid` 和 `index_oid`
- 把元数据持久化到 catalog meta page 链
- 在重启时自动恢复 `TableInfo / TableHeap / BPlusTree`

#### 3.2 TableInfo 的职责

`TableInfo` 不是表数据本身，而是一张表的运行时对象壳：

- `oid`
- `name`
- `schema`
- `table_heap`
- `indexes`

因此，执行层和上层代码通常不直接持有一堆离散对象，而是通过 `TableInfo` 进入一张表。

#### 3.3 Schema / Column 的职责

- `Column` 定义列名、类型、长度、nullable、默认值等元数据
- `Schema` 负责把多列组织起来，并计算 tuple 布局与列偏移

换句话说：

- `Schema` 决定“逻辑上这张表长什么样”
- `TableHeap` 决定“物理上这些 tuple 放在哪里”
- `TableInfo` 把两者挂到同一张表对象上

### 4. Table 与 Record：表级数据入口

关键文件：

- `CXX/src/include/table/table_heap.h`
- `CXX/src/include/table/table_iterator.h`
- `CXX/src/include/storage/page/table_page.h`
- `CXX/src/include/storage/record/rid.h`
- `CXX/src/include/storage/record/tuple.h`
- `CXX/src/include/storage/record/tuple_codec.h`

这是系统真正开始接触“表数据本体”的地方。

#### 4.1 TableHeap 管什么

`TableHeap` 是“表级堆组织”，它不关心 SQL，也不关心列语义，它关心的是：

- 首页页号是什么
- 尾页在哪里
- 哪些页还有空闲空间
- 插入应该落到哪一页
- 更新时是否需要迁移到新页
- 删除后是否可能回收空页
- 如何沿页链遍历整张表

可以把它理解为：`TablePage` 只会管单页内部，`TableHeap` 才真正把“跨多个页的一张表”组织起来。

#### 4.2 Record 子层负责什么

- `RID`：记录的物理地址，形如 `(page_id, slot_id)`
- `Tuple`：原始字节载体
- `TupleCodec`：在 `vector<Value>` 和二进制 tuple 之间做编解码

这层的作用非常关键，因为它是执行层和物理存储之间的桥：

- 执行器读到的是 `Value`
- 页里存的是二进制 tuple
- `TupleCodec` 负责在这两者之间往返

#### 4.3 TableIterator 的位置

`TableIterator` 是顺序扫描的游标对象。`SeqScanExecutor` 并不自己维护页遍历逻辑，而是直接复用 `TableHeap` 暴露出来的迭代器能力。

### 5. Index：独立于表页链的键访问路径

关键文件：

- `CXX/src/include/storage/index/b_plus_tree.h`
- `CXX/src/include/storage/index/index_iterator.h`
- `CXX/src/include/storage/page/b_plus_tree_page.h`
- `CXX/src/include/storage/page/b_plus_tree_leaf_page.h`
- `CXX/src/include/storage/page/b_plus_tree_internal_page.h`

索引层当前实现的是整数键 B+Tree。

#### 5.1 BPlusTree 负责什么

- 管理 `root_page_id_`
- 管理 `header_page_id_`
- 从根一路下降到叶子
- 处理插入分裂与删除重平衡
- 提供范围扫描迭代器
- 把根页信息持久化到 header page

#### 5.2 IndexIterator 负责什么

它只负责在叶子页链上顺序前进，返回 `(key, RID)` 对；真正“回表解码 tuple”的动作是在 `IndexScanExecutor` 里完成的。

#### 5.3 当前索引路径的特点

- 索引对象挂在 `TableInfo` 下，不独立漂浮在系统外面
- `Catalog::CreateIndex()` 会回填已有表数据
- `IndexScanExecutor` 遇到 stale RID 时会跳过而不是直接失败
- 因此，索引层本身负责“键到 RID”，执行层负责“RID 到行值”

### 6. Buffer：所有页访问的中间层

关键文件：

- `CXX/src/include/buffer/buffer_pool_manager/buffer_pool_manager.h`
- `CXX/src/include/buffer/replacer/lru_k_replacer.h`

缓冲层是整个系统里最典型的“承上启下层”：

- 上面连接 `TableHeap`、`BPlusTree`、恢复逻辑
- 下面连接 `DiskManager`

`BufferPoolManager` 解决的是“页如何在内存中驻留和替换”的问题，核心职责包括：

- `page_id -> frame_id` 映射
- 空闲 frame 管理
- 被 pin 页与可淘汰页管理
- 通过 `LruKReplacer` 做淘汰选择
- 在页进入和离开内存时与 `DiskManager` 协作

所有真正落到表页、索引页的读写，最后都要经过这一层。

### 7. Storage / WAL：真正与磁盘状态打交道的层

关键文件：

- `CXX/src/include/storage/disk/disk_manager.h`
- `CXX/src/include/storage/page/page.h`
- `CXX/src/include/storage/page/table_page.h`
- `CXX/src/include/storage/wal/wal_manager.h`

#### 7.1 DiskManager

`DiskManager` 负责数据库文件的页级 I/O，是页模型与文件模型之间的转换器。它管理：

- `DBHeader`
- `next_page_id`
- `free_list_head`
- `catalog_meta_page_id`
- 普通页读写
- 页分配和回收

数据库文件当前最重要的持久化入口是：

- Page 0 存放 `DBHeader`
- `DBHeader.catalog_meta_page_id` 指向 `Catalog` 元数据页链入口

#### 7.2 Page / TablePage / B+TreePage

这些对象是页内语义层：

- `Page`：内存中的通用页对象，带 metadata、pin count、dirty 标记、读写锁
- `TablePage`：负责单页 tuple slot 布局
- `BPlusTreePage` 及其子类：负责 B+Tree 节点页布局

一句话区分它们：

- `DiskManager` 负责“把整页读写到文件”
- `BufferPoolManager` 负责“把整页缓存到内存”
- `TablePage / B+TreePage` 负责“解释页内字节该怎么用”

#### 7.3 WalManager

当前 WAL 是最小可用 redo WAL，采用整页 after-image 策略：

- 每条记录携带完整页面镜像
- `TableHeap` 修改页面后先 `AppendLog()`，再 `FlushLog()`
- 重启时 `Recover()` 顺序扫描 WAL，调用 `Redo()` 覆盖目标页
- 恢复成功后统一刷盘，并截断整个 WAL 文件

当前 WAL 的能力边界也需要明确：

- 只有 redo，没有 undo
- 没有 checkpoint
- 日志粒度是整页 after-image，不是逻辑操作
- 当前页级写入路径中，`TableHeap` 是主要的 WAL 触发者

## 四条关键数据路径

### 1. 启动与恢复路径

```text
DatabaseRuntime::Open()
  -> DiskManager
  -> BufferPoolManager
  -> WalManager::Recover()      [可选]
  -> Catalog::LoadCatalogMeta()
  -> rebuild TableInfo / TableHeap / BPlusTree
  -> bind WalManager
  -> ExecutorContext
```

这是当前整个系统的“零号主线”。

### 2. 插入路径

```text
ValuesExecutor
  -> InsertExecutor
  -> TupleCodec::Encode(schema, values)
  -> TableHeap::InsertTuple(tuple)
  -> TablePage::InsertTuple(slot allocation)
  -> WAL append + flush         [如果表已绑定 WalManager]
  -> BufferPoolManager::UnpinPage(is_dirty=true)
  -> index_maintenance::InsertIntoIndexesByKey()   [如果表有索引]
```

这里最值得注意的是两点：

- tuple 写入和索引写入都已经纳入执行层 DML 路径
- 执行器层面已经承担了“数据修改 + 索引同步”的协调职责

### 3. 顺序扫描路径

```text
SeqScanExecutor
  -> TableHeap::Begin()
  -> TableIterator
  -> TablePage / RID / Tuple
  -> TupleCodec::Decode(schema, tuple)
  -> ExecutorRow
  -> FilterExecutor / ProjectionExecutor   [可选]
```

### 4. 索引扫描路径

```text
IndexScanExecutor
  -> BPlusTree::Begin(start_key)
  -> IndexIterator yields (key, RID)
  -> TableHeap::GetTuple(RID)              [如果提供了 TableInfo]
  -> TupleCodec::Decode()
  -> ExecutorRow
```

如果没有 `TableInfo`，`IndexScanExecutor` 也可以退化成“只返回 key/RID 语义”的扫描器。

## 每层边界再说一遍

为了避免后续开发再次回到“局部修改、整体失序”，这里把每层边界压缩成最短版本：

- `DatabaseRuntime`
  - 负责装配与启动顺序
  - 不负责具体算子逻辑
- `Executor`
  - 负责行流转与 DML 协调
  - 不负责页缓存和页布局
- `Catalog`
  - 负责对象目录、元数据持久化与恢复
  - 不负责 tuple 字节读写
- `TableHeap`
  - 负责表页链和表级记录操作
  - 不负责列语义和查询计划
- `BPlusTree`
  - 负责键到 RID 的组织
  - 不负责 tuple 解码
- `BufferPoolManager`
  - 负责页驻留、替换、刷回
  - 不负责 schema 和索引语义
- `DiskManager`
  - 负责数据库文件和页级 I/O
  - 不负责缓存策略
- `WalManager`
  - 负责 redo 恢复材料
  - 不负责事务级别的回滚语义

## 当前源码阅读顺序

如果接下来要继续做系统化整理，推荐按下面顺序读代码：

1. `CXX/src/include/runtime/database_runtime.h`
2. `CXX/src/runtime/database_runtime.cxx`
3. `CXX/src/include/execution/` 和 `CXX/src/execution/`
4. `CXX/src/include/catalog/` 和 `CXX/src/catalog/catalog.cxx`
5. `CXX/src/include/table/` 和 `CXX/src/table/table_heap.cxx`
6. `CXX/src/include/storage/index/` 与 `CXX/src/storage/index/`
7. `CXX/src/include/buffer/` 与 `CXX/src/buffer/`
8. `CXX/src/include/storage/disk/`, `CXX/src/include/storage/wal/`
9. `CXX/src/include/type/` 与 `CXX/src/include/common/`

这个顺序和系统真实依赖方向是一致的，更适合继续做架构收敛。

## 目录结构速览

```text
CXX/
├── src/
│   ├── runtime/          # 统一运行入口
│   ├── execution/        # 执行器树
│   ├── catalog/          # 表/索引目录与元数据持久化
│   ├── table/            # 表级堆组织与迭代器
│   ├── buffer/           # 缓冲池与替换器
│   ├── storage/
│   │   ├── disk/         # 数据库文件页 I/O
│   │   ├── page/         # 页结构
│   │   ├── record/       # RID / Tuple / TupleCodec
│   │   ├── index/        # B+Tree / IndexIterator
│   │   └── wal/          # WAL redo
│   ├── type/             # TypeId / Value
│   └── include/          # 公共头文件
└── test/                 # 分层测试与功能测试
```

## 构建与测试

```bash
cmake -S CXX -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## C/Go API 门禁（单入口）

Go 外壳默认链接 `CXX/build/src/capi/libharuhidb_capi.so`。为了避免“Go 链接旧 `.so`”的假失败，C/Go API 相关改动建议统一走单入口门禁脚本：

```bash
./scripts/api_gate.sh
```

这个脚本会固定执行顺序：

1. 先配置并构建 C API 动态库 + `capi_test`
2. 再执行 C API 相关 CTest（`CApiTest.*`）
3. 最后执行 `go test ./...`

若 C API 变更但 Go 包装未对齐，门禁会在最后一步直接失败并给出可定位输出。

测试目前按模块拆分，重点覆盖：

- `storage`：页、索引、WAL、磁盘
- `buffer`：缓冲池与替换策略
- `catalog`：表/索引元数据持久化与恢复
- `table`：堆表操作与 WAL 路径
- `execution`：执行器行为、恢复后的可用性、索引自动维护

## 这份 README 的使用方式

后续如果继续整理项目，建议始终围绕下面三个问题往下推进：

1. 这一层只负责什么，不负责什么？
2. 这一层依赖哪些下层对象？
3. 这一层向上层暴露的稳定入口是什么？

只要这个骨架始终不丢，后面无论继续补 SQL、事务还是优化器，项目都还能沿着同一条主线演进，而不是再次散成一堆局部修补。

