# Go 完整 API 与 Action 动作集覆盖矩阵

这份文档把两层能力放在同一张图里：

- Go 侧 `haruhidb` 包实际可用 API（完整能力）
- Action Protocol `v1 / v2` 对外暴露动作（协议能力）

结论先说：

- Go 层能力完整，覆盖 DDL + DML + 元数据。
- `v1` 保持“既有表上的稳定读写闭环”。
- `v2` 在 `v1` 基础上新增 4 个 DDL 动作，实现完整生命周期动作集。

## 0) Go 侧对外入口（当前形态）

从使用者角度，Go 侧有 3 层对外接口：

1. CLI 层（`go run ./cmd/haruhidb ...`）
- 子命令仅保留：`serve`

2. HTTP 层（`serve` 启动后）
- `GET /healthz`
- `POST /v1/action`
- `POST /v1/nl/translate`
- `GET /ui`（内置网页）

3. 包 API 层（`GO/haruhidb/haruhidb.go`）
- 最底层 Go 调用面，覆盖 DDL + DML + 元数据 + 扫描

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

## 2) Action 动作集（v1 / v2）

### v1 动作集（10）

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

### v2 增量动作（4）

在 v1 基础上新增：

1. `create_table`
2. `drop_table`
3. `create_primary_int_index`
4. `drop_index`

## 3) 覆盖矩阵（Go API -> Action v1 / v2）

| Go API | v1 | v2 | 备注 |
| --- | --- | --- | --- |
| `ListTables` | `list_tables` | `list_tables` | 已覆盖 |
| `TableExists` | `table_exists` | `table_exists` | 已覆盖 |
| `ListTableColumns/ListTableIndexes` | `describe_table` | `describe_table` | 已覆盖 |
| `GetRowByPrimaryInt` | `get_by_primary_int` | `get_by_primary_int` | 需 primary-int index |
| `ScanAll` | `scan_all` | `scan_all` | 支持 limit |
| `ScanByPrimaryIntRange` | `scan_primary_int_range` | `scan_primary_int_range` | 需 primary-int index |
| `InsertRow` | `insert_row` | `insert_row` | 已覆盖 |
| `UpdateRowByPrimaryInt` | `update_by_primary_int` | `update_by_primary_int` | 已覆盖 |
| `DeleteRowByPrimaryInt` | `delete_by_primary_int` | `delete_by_primary_int` | 已覆盖 |
| `CreateTable` | 不支持 | `create_table` | v2 新增 |
| `DropTable` | 不支持 | `drop_table` | v2 新增 |
| `CreatePrimaryIntIndex` | 不支持 | `create_primary_int_index` | v2 新增 |
| `DropIndex` | 不支持 | `drop_index` | v2 新增 |

## 4) 闭环能力结论

1. v1 闭环
- 已有表上的读写闭环完整（查/增/改/删/批处理）。

2. v2 闭环
- 覆盖“空库到建表建索引到读写再清理”的完整动作集语义。

## 5) 网页测试推荐

- v1 示例：见 [Action v1 功能演示](action-v1-showcase-example.md)
- v2 示例：见 [Action v2 功能演示](action-v2-showcase-example.md)

## 6) 兼容性说明

- `version=v1` 与 `version=v2` 并存。
- v1 用户无破坏性影响。
- v2 仅在显式请求 `version=v2` 时生效。