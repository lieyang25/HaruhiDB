# Go 完整 API 与 Action 动作集覆盖矩阵

这份文档把两层能力放在同一张图里：

- Go 侧 `haruhidb` 包实际可用 API（完整能力）
- Action Protocol v1 对外暴露动作（当前能力）

结论先说：

- Go 层能力比 Action v1 更完整。
- Action v1 已覆盖“既有表上的读写闭环”，但未覆盖 DDL。
- 如果要补 DDL，建议新增 `v2`，保持 `v1` 完全兼容。

## 0) Go 侧对外入口（完整视角）

从使用者角度，Go 侧有 3 层对外接口：

1. CLI 层（`go run ./cmd/haruhidb ...`）
- 子命令：`serve`、`run`、`nl`、`shell`

2. HTTP 层（`serve` 启动后）
- `GET /healthz`
- `POST /v1/action`
- `POST /v1/nl/translate`
- `GET /ui`（内置网页）

3. 包 API 层（`GO/haruhidb/haruhidb.go`）
- 最底层的 Go 调用面，覆盖 DDL + DML + 元数据 + 扫描

## 1) Go 侧完整 API（`GO/haruhidb/haruhidb.go`）

### 连接与能力

- `APIVersion() string`
- `Capabilities() Capability`
- `Open(path string, opts OpenOptions) (*DB, error)`
- `(*DB).Close() error`

### Schema / DDL

- `(*DB).CreateTable(name string, columns []ColumnDef) error`
- `(*DB).DropTable(tableName string) error`
- `(*DB).CreatePrimaryIntIndex(tableName, indexName string) error`
- `(*DB).DropIndex(tableName, indexName string) error`

### 数据读写 / DML

- `(*DB).InsertRow(tableName string, values []Value) error`
- `(*DB).UpdateRowByPrimaryInt(tableName string, key int32, values []Value) (int, error)`
- `(*DB).DeleteRowByPrimaryInt(tableName string, key int32) (int, error)`
- `(*DB).GetRowByPrimaryInt(tableName string, key int32) (Row, bool, error)`

### 扫描查询

- `(*DB).ScanAll(tableName string) (*Scanner, error)`
- `(*DB).ScanByPrimaryIntRange(tableName string, startKey, endKey int32) (*Scanner, error)`
- `(*Scanner).Next() (Row, error)`
- `(*Scanner).Close() error`

### 元数据

- `(*DB).TableExists(name string) (bool, error)`
- `(*DB).ListTables() ([]string, error)`
- `(*DB).ListTableColumns(tableName string) ([]ColumnInfo, error)`
- `(*DB).ListTableIndexes(tableName string) ([]string, error)`

## 2) Action Protocol v1 动作集（当前对外）

v1 共 10 个动作：

1. `list_tables`
2. `table_exists`
3. `describe_table`
4. `get_by_primary_int`
5. `scan_all`
6. `scan_primary_int_range`
7. `insert_row`
8. `update_by_primary_int`
9. `delete_by_primary_int`
10. `batch`

## 3) 覆盖矩阵（Go API -> Action v1）

| Go API | v1 动作是否暴露 | 备注 |
| --- | --- | --- |
| `ListTables` | 是 (`list_tables`) | 已暴露 |
| `TableExists` | 是 (`table_exists`) | 已暴露 |
| `ListTableColumns/ListTableIndexes` | 是 (`describe_table`) | 已暴露 |
| `GetRowByPrimaryInt` | 是 (`get_by_primary_int`) | 需 primary-int index |
| `ScanAll` | 是 (`scan_all`) | 支持 limit |
| `ScanByPrimaryIntRange` | 是 (`scan_primary_int_range`) | 需 primary-int index |
| `InsertRow` | 是 (`insert_row`) | 已暴露 |
| `UpdateRowByPrimaryInt` | 是 (`update_by_primary_int`) | 已暴露 |
| `DeleteRowByPrimaryInt` | 是 (`delete_by_primary_int`) | 已暴露 |
| `CreateTable` | 否 | v1 未暴露 |
| `DropTable` | 否 | v1 未暴露 |
| `CreatePrimaryIntIndex` | 否 | v1 未暴露 |
| `DropIndex` | 否 | v1 未暴露 |

## 4) 现在是否“完整闭环”

分两种语义：

1. “既有表上的业务读写闭环”
- 是。v1 已能完成查、增、改、删、批处理。

2. “从空库到建表到建索引到读写的全生命周期闭环”
- 否。因为缺少 DDL 动作。

## 5) 如果扩到 v2（建议动作）

建议 `Action Protocol v2` 新增 4 个动作：

1. `create_table`
2. `drop_table`
3. `create_primary_int_index`
4. `drop_index`

这样可达成“空库 -> 建模 -> 写入 -> 查询 -> 清理”的完整自动化闭环。

## 6) v2 对现有功能的影响评估

前提：采用“版本显式分层”策略（`version=v1` 与 `version=v2` 并存）。

### 结论

- 对现有 v1 用户：无破坏性影响。
- 对现有 UI/CLI：默认继续按 v1 行为工作。
- 对模型提示：仅在选择 v2 时注入 v2 动作规范。

### 可能风险与控制

1. 风险：模型在 v1 下误用 v2 动作
- 控制：校验器按 `version` 严格拒绝未知动作。

2. 风险：文档与 examples 混用导致提示词漂移
- 控制：`examples_path` 分 v1/v2 两套文件。

3. 风险：`create_table` 参数设计不稳定
- 控制：先发布最小字段集（列名/类型/nullable/length），后续小步扩展。

### 迁移建议

- 默认保持 `v1`
- 新功能只在 `v2` 增量开放
- 通过配置或 UI 显式选择协议版本