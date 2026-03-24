# Go API 与 Action 覆盖矩阵（Web-Only）

## 1. Go 运行入口

- CLI 子命令：`serve`
- 包入口：`GO/haruhidb/haruhidb.go`

## 2. HTTP 接口矩阵

| 接口 | 方法 | 用途 |
| --- | --- | --- |
| `/healthz` | GET | 健康检查 |
| `/v1/capabilities` | GET | 查询协议版本、动作清单、按 mode 分类、表清单 |
| `/v1/action` | POST | 执行动作 |
| `/ui` | GET | Web 控制台 |

已移除：`POST /v1/nl/translate`。

## 3. 动作覆盖（14）

| 分类 | 动作 |
| --- | --- |
| metadata | `list_tables`, `table_exists`, `describe_table` |
| read | `get_by_primary_int`, `scan_all`, `scan_primary_int_range` |
| write | `insert_row`, `update_by_primary_int`, `delete_by_primary_int` |
| ddl | `create_table`, `drop_table`, `create_primary_int_index`, `drop_index` |
| batch | `batch` |

## 4. mode 规则

- `read_only`：只允许 metadata/read/batch(只读子动作)
- `read_write`：允许全部动作

## 5. 运行时能力发现

前端或调用方应优先使用 `/v1/capabilities` 动态获取动作能力，而不是写死动作可用性。
