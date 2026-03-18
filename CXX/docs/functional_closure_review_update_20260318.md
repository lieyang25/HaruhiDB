# HaruhiDB 功能闭环审查更新（2026-03-18）

## 1. 更新结论

本次实现后，`Catalog` 已从“仅内存目录”升级为“最小持久化目录”。

系统当前定位更新为：

**半闭环单机内核（已补齐 Catalog 持久化与重启自动发现）**

原因：

- 已闭环：表/索引元数据持久化、重启自动发现、`schema/first_page_id/header_page_id` 自动恢复。
- 未闭环：执行层 DML 仍不会自动维护索引（索引与表数据仍可能出现 stale 关系），也没有统一 `Database` 启动编排对象。

## 2. 本次已落地能力

### 2.1 数据库级元数据入口

- `DBHeader` 新增 `catalog_meta_page_id`。
- `DiskManager` 新增：
  - `CatalogMetaPageId()`
  - `SetCatalogMetaPageId(page_id_t)`

这让 Catalog 元数据入口从“进程变量”变成“数据库文件内可恢复入口”。

### 2.2 Catalog 元数据持久化对象

已按最小集合落地：

- `TableMeta = {table_oid, table_name, serialized_schema, first_page_id}`
- `IndexMeta = {index_oid, table_oid, index_name, header_page_id}`

并持久化 `next_table_oid` / `next_index_oid`。

### 2.3 Catalog 自动恢复链路

`Catalog` 构造时自动执行：

1. 从 `DiskManager` 读取 `catalog_meta_page_id`
2. 读取 Catalog 元数据页链
3. 重建 `TableInfo`
4. 用 `first_page_id` 重建 `TableHeap`
5. 用 `header_page_id` 重建 `BPlusTree`

### 2.4 变更后自动落盘

以下操作已自动触发 Catalog 元数据持久化：

- `Catalog::CreateTable`
- `Catalog::CreateIndex`
- `Catalog::LoadIndex`

## 3. 当前功能闭环矩阵（更新后）

| 功能 | 当前状态 | 说明 |
| --- | --- | --- |
| 表元数据持久化 | 已闭环可用 | 重启后可自动发现表与 schema |
| 索引元数据持久化 | 已闭环可用 | 重启后可自动重建索引对象 |
| 表页与索引页持久化 | 已闭环可用 | 依赖刷盘/WAL 机制 |
| WAL redo | 已闭环可用 | 需先显式执行 `WalManager::Recover` |
| 重启后对象自动装配 | 已闭环可用（最小） | `Catalog` 构造自动装配 |
| DML 自动维护索引 | 底层能力存在但上层未闭环 | 仍需手工回填/维护索引 |

## 4. 新增/调整测试覆盖

本次新增并通过：

- `CatalogTableInfoTest.CatalogAutoDiscoversTableAndSchemaAfterRestart`
- `CatalogTableInfoTest.CatalogAutoRecoversIndexFromHeaderPageId`
- `CatalogTableInfoTest.WalRecoverThenCatalogLoadKeepsEntrypointsConsistent`

并回归通过关键基线（含全量 `ctest`）：

- `DiskManagerTest.PersistenceTest`
- `BufferPoolManagerTest.DataPersistsAcrossManagerRestart`
- `BPlusTreeTest.HeaderPagePersistsRootAndRecoversTree`
- `WalManagerTest.AppendFlushAndRecoverRedoAppliesAfterImage`
- `TableHeapWalTest.InsertThenRecover`
- `TableHeapWalTest.NewPageLinkingSurvivesRecover`
- `TableHeapWalTest.RecoveryDoesNotGuaranteeIndexConsistency`
- `ExecutorFunctionalTest.InsertExecutorMixedValidInvalidInputLeavesPartialWrites`
- `ExecutorFunctionalTest.IndexScanSkipsStaleEntries`

## 5. 仍缺的最小闭环项

如果目标是“更像最小完整数据库”，下一步最关键是：

1. 提供统一启动入口（固定 `Recover -> LoadCatalog -> 构造执行上下文`）。
2. 在 DML 路径上自动维护索引，消除 stale 索引条目依赖。
