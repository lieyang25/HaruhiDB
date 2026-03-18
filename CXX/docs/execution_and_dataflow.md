# HaruhiDB 运行与数据链路

## 1. 启动链路

当前系统统一从 `runtime::DatabaseRuntime::Open()` 启动。

它做的事情不是简单 new 几个对象，而是严格按依赖顺序完成装配：

```text
DatabaseRuntime::Open(db_path, options)
  -> DiskManager(db_path)
  -> BufferPoolManager(pool_size, disk_manager, lru_k)
  -> WalManager(wal_path) + Recover(buffer_pool)      [enable_wal 时]
  -> Catalog(buffer_pool)
  -> Catalog::BindWalManager(wal_manager)
  -> ExecutorContext(catalog)
```

这条顺序有两个关键点：

- WAL 恢复发生在 Catalog 装载之前，先把页状态恢复对
- Catalog 装载发生在 ExecutorContext 创建之前，先把数据库对象恢复对

## 2. 执行层的组织方式

### 2.1 基本协议

所有执行器都遵循同一个接口：

- `Init()`：重置内部状态
- `Next(ExecutorRow*)`：产生下一行，直到 EOF

执行层统一通过 `ExecutorRow` 传递结果：

- `values`：当前行的值列表
- `rid`：对应物理记录位置
- `has_rid`：当前行是否携带合法 RID

这让执行层可以同时支持：

- 纯内存数据源，如 `ValuesExecutor`
- 基于表的扫描，如 `SeqScanExecutor`
- 基于索引的扫描，如 `IndexScanExecutor`
- 依赖 RID 的 DML，如 `DeleteExecutor` 和 `UpdateExecutor`

### 2.2 当前执行器分类

#### 数据源执行器

- `ValuesExecutor`
- `SeqScanExecutor`
- `IndexScanExecutor`

#### 行变换执行器

- `FilterExecutor`
- `ProjectionExecutor`

#### 数据修改执行器

- `InsertExecutor`
- `DeleteExecutor`
- `UpdateExecutor`

## 3. 顺序扫描链路

顺序扫描的真实路径如下：

```text
SeqScanExecutor
  -> TableInfo
  -> TableHeap::Begin()
  -> TableIterator
  -> TablePage / RID / Tuple
  -> TupleCodec::Decode(schema, tuple)
  -> ExecutorRow
```

这里每一层的角色分别是：

- `SeqScanExecutor`：把表扫描包装成统一执行器接口
- `TableIterator`：跨页推进扫描游标
- `TableHeap`：负责整张表的页链组织
- `TupleCodec`：把二进制 tuple 还原成 `Value` 列表

所以执行层和存储层真正的衔接点不是 `DiskManager`，而是：

- `TableHeap`
- `RID / Tuple`
- `TupleCodec`

## 4. 索引扫描链路

索引扫描的真实路径如下：

```text
IndexScanExecutor
  -> BPlusTree::Begin(start_key)
  -> IndexIterator yields (key, RID)
  -> TableHeap::GetTuple(RID)              [如果提供了 TableInfo]
  -> TupleCodec::Decode(schema, tuple)
  -> ExecutorRow
```

这条链路里最重要的分工是：

- `BPlusTree` 只负责找到 `(key, RID)`
- `IndexScanExecutor` 负责决定是否回表
- `TableHeap` 负责根据 RID 取 tuple
- `TupleCodec` 负责把 tuple 解码成值

如果 `IndexScanExecutor` 没有绑定 `TableInfo`，它可以退化成“只返回键和 RID 语义”的扫描器。

当前实现还约定：

- 如果索引里出现 stale RID，`IndexScanExecutor` 会跳过而不是立即报错

这让扫描行为更稳，但也意味着索引一致性最终仍依赖上层写路径是否规范。

## 5. 插入链路

插入经过执行层时，真实流程如下：

```text
ValuesExecutor / child executor
  -> InsertExecutor
  -> TupleCodec::Encode(schema, values)
  -> TableHeap::InsertTuple(tuple)
  -> TablePage::InsertTuple(...)
  -> Append WAL after-image + FlushLog     [如果表已绑定 WAL]
  -> BufferPoolManager::UnpinPage(..., dirty=true)
  -> execution::detail::InsertIntoIndexesByKey()   [如果表有索引]
```

这条链路说明了两个事实：

- 表写入和索引写入当前是在执行层里统一协调的
- `TableHeap` 自身并不理解“索引应该怎么改”，它只负责表数据

## 6. 删除链路

删除经过执行层时，真实流程如下：

```text
child executor (must provide RID)
  -> DeleteExecutor
  -> 从输入行提取索引键
  -> 先从 indexes 删除 key -> RID
  -> 再调用 TableHeap::DeleteTuple(rid)
  -> 失败时尽量回滚索引删除
```

这里可以看出当前删除路径的核心策略是：

- 执行层负责编排“索引先删、表再删”的顺序
- `TableHeap` 只提供记录删除原语
- 索引回滚是 best-effort，不是事务语义

## 7. 更新链路

更新经过执行层时，真实流程如下：

```text
child executor (must provide RID)
  -> UpdateExecutor
  -> 读取旧值并提取旧索引键
  -> 调用 updater 生成新值
  -> 检查索引键是否变化
  -> TupleCodec::Encode(new values)
  -> TableHeap::UpdateTuple(old_rid, new_tuple, &new_rid)
  -> 如果 RID 迁移，则重绑 indexes 里的 RID
```

当前更新语义有一个非常重要的限制：

- 更新不允许修改索引键本身

原因不是概念上不能支持，而是当前实现把索引维护简化成：

- 键不变时，只需要在记录迁移时重绑 RID
- 键变化时，逻辑会明显复杂得多

因此这是当前实现的明确边界，而不是隐藏细节。

## 8. Catalog 与执行层的衔接方式

当前执行层并不会自己管理数据库对象生命周期，它依赖 `ExecutorContext` 提供 `Catalog`。

而 `Catalog` 再把对象关系组织成：

```text
Catalog
  -> TableInfo
       +-- Schema
       +-- TableHeap
       `-- BPlusTree indexes
```

因此，执行层真正依赖的并不是很多离散对象，而是：

- `ExecutorContext` 给它入口
- `TableInfo` 给它一张表的完整运行时视图

这是当前系统比较稳定的结构点。

## 9. 当前数据链路的几个结论

### 9.1 读路径相对干净

不管是顺序扫描还是索引扫描，当前读路径的层次都比较清楚：

- 执行器负责行协议
- `TableHeap / BPlusTree` 负责访问路径
- `TupleCodec` 负责值和 tuple 的转换

### 9.2 写路径主要靠执行层维持一致性

当前表和索引的一致性，主要依赖经过执行层的标准 DML 路径。

这意味着：

- 经由执行器写数据，表和索引关系通常是可控的
- 绕过执行器直接操作 `TableHeap`，则需要调用方自行维护索引一致性

### 9.3 运行入口已经固定下来

`DatabaseRuntime::Open()` 现在已经把：

- 恢复
- 装载目录
- 构造执行上下文

收束成固定流程，这对后续继续扩展 SQL 或上层接口很重要，因为至少启动主线已经稳定了。
