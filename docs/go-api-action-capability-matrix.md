# Go 完整 API 与 Action v3 覆盖矩阵

这份文档把两层能力放在同一张图里：

- Go 侧 `haruhidb` 包实际可用 API（完整能力）
- Action Protocol `v3` 对外暴露动作（协议能力）

结论先说：

- Go 层能力完整，覆盖 DDL + DML + 元数据。
- 协议层现在统一为 `v3`，不再区分动作集 `v1/v2`。
- 服务端仍兼容接收 `v1/v2` 输入，但会按 `v3` 语义执行与归一化。

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

## 2) Action 动作集（v3 统一集）

`v3` 动作集包含 14 个动作：

1. `list_tables`
2. `table_exists`
3. `describe_table`
4. `get_by_primary_int`
5. `scan_all`
6. `scan_primary_int_range`
7. `insert_row`
8. `update_by_primary_int`
9. `delete_by_primary_int`
10. `create_table`
11. `drop_table`
12. `create_primary_int_index`
13. `drop_index`
14. `batch`

## 3) 覆盖矩阵（Go API -> Action v3）

| Go API | v3 | 备注 |
| --- | --- | --- |
| `ListTables` | `list_tables` | 已覆盖 |
| `TableExists` | `table_exists` | 已覆盖 |
| `ListTableColumns/ListTableIndexes` | `describe_table` | 已覆盖 |
| `GetRowByPrimaryInt` | `get_by_primary_int` | 需 primary-int index |
| `ScanAll` | `scan_all` | 支持 limit |
| `ScanByPrimaryIntRange` | `scan_primary_int_range` | 需 primary-int index |
| `InsertRow` | `insert_row` | 已覆盖 |
| `UpdateRowByPrimaryInt` | `update_by_primary_int` | 已覆盖 |
| `DeleteRowByPrimaryInt` | `delete_by_primary_int` | 已覆盖 |
| `CreateTable` | `create_table` | 已覆盖 |
| `DropTable` | `drop_table` | 已覆盖 |
| `CreatePrimaryIntIndex` | `create_primary_int_index` | 已覆盖 |
| `DropIndex` | `drop_index` | 已覆盖 |

## 4) 闭环能力结论

1. 生命周期闭环
- 覆盖“空库 -> 建表建索引 -> 读写查询 -> 清理”的完整动作语义。

2. 协议稳定性
- 统一 `v3` 语义，避免 `v1/v2` 分叉导致的版本判断错误。

## 5) 网页测试推荐

- 打开 `GET /ui`
- 先调用 `/v1/nl/translate` 预览候选 envelope
- 确认后调用 `/v1/action` 执行

## 6) 兼容性说明

- 服务端兼容接收 `version=v1/v2/v3`。
- 任一版本输入都会按统一 `v3` 语义执行。
- NL 归一化输出以 `v3` 为准。
