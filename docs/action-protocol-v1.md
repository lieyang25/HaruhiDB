# HaruhiDB Action Protocol v1

> 先读建议：日常联调优先看 [一页上手与动作速查](web-action-one-page-guide.md)。
> 
> 历史命名说明：本文件名保留为 `v1`，但当前服务已统一按 `v3` 语义处理请求（`v1/v2/v3` 均可输入，内部会 canonicalize 到 `v3`）。

HaruhiDB 动作协议 v1 采用“统一请求信封 + 动作枚举 + 强结构参数”模型。

面向模型训练/提示词注入可直接使用：

- [请求模板 JSON](action-v1-request-template.json)
- [动作与参数机器可读规范 JSON](action-v1-model-spec.json)

## 设计目标

- 默认一次请求只做一个动作；`batch` 动作可顺序执行多个子动作
- 动作名固定枚举，不使用自由 DSL
- `args` 必须是 JSON 对象，并且拒绝未知字段
- 行和值统一使用“列名 -> JSON 标量”对象，不暴露底层 `[]Value`
- 响应统一使用 `ok / data / error / meta` 包装

## 当前边界（以运行时为准）

当前实现可用动作（含 DDL）：

| 动作 | 支持 mode | 备注 |
| --- | --- | --- |
| `list_tables` | `read_only`, `read_write` | 元数据读取 |
| `table_exists` | `read_only`, `read_write` | 元数据读取 |
| `describe_table` | `read_only`, `read_write` | 元数据读取 |
| `get_by_primary_int` | `read_only`, `read_write` | 依赖 primary-int index |
| `scan_all` | `read_only`, `read_write` | `limit` 可选，默认 `100` |
| `scan_primary_int_range` | `read_only`, `read_write` | 依赖 primary-int index，`limit` 可选 |
| `insert_row` | `read_write` | 写动作 |
| `update_by_primary_int` | `read_write` | 写动作 |
| `delete_by_primary_int` | `read_write` | 写动作 |
| `create_table` | `read_write` | DDL |
| `drop_table` | `read_write` | DDL |
| `create_primary_int_index` | `read_write` | DDL |
| `drop_index` | `read_write` | DDL |
| `batch` | `read_only`, `read_write` | 顺序执行子动作 |

通用约束：

- `mode` 只接受 `read_only` 和 `read_write`
- `read_only` 不允许写动作与 DDL 动作
- `batch` 子动作继承外层 `mode`
- 所有 `*_by_primary_int` / `scan_primary_int_range` 要求目标表首列为 `INTEGER NOT NULL`
- `get_by_primary_int` / `scan_primary_int_range` 额外要求目标表已建 primary-int index
- `batch` 不是事务；默认遇错继续，`stop_on_error=true` 时在首个失败子动作后停止

`v1` 的历史语义仍可兼容输入，但新请求建议统一按 `v3` 编写。


## 与 Go API 的覆盖关系

如果你关心“Go 底层能力”和“协议层暴露能力”的对应关系，见：

- [Go 完整 API 与动作集覆盖矩阵](go-api-action-capability-matrix.md)

当前运行时已公开 DDL 动作（`create_table` / `drop_table` / `create_primary_int_index` / `drop_index`）。
本文件名保留为 `v1` 仅为历史兼容，不再代表“仅 v1 动作子集”。

## 请求信封

```json
{
  "version": "v3",
  "request_id": "req-001",
  "mode": "read_only",
  "action": "list_tables",
  "args": {}
}
```

字段约束：

- `version`：接受 `"v1"` / `"v2"` / `"v3"`，运行时统一按 `v3` 处理
- `request_id`：必须为非空字符串；推荐使用 UUID，但 v1 不强制格式
- `mode`：`"read_only"` 或 `"read_write"`
- `action`：固定枚举
- `args`：动作参数对象；必须是 JSON 对象

## 响应信封

```json
{
  "ok": true,
  "request_id": "req-001",
  "action": "list_tables",
  "data": {
    "tables": [
      "users",
      "orders"
    ]
  },
  "error": null,
  "meta": {}
}
```

失败响应：

```json
{
  "ok": false,
  "request_id": "req-001",
  "action": "list_tables",
  "data": null,
  "error": {
    "code": "INTERNAL",
    "message": "database is closed"
  },
  "meta": {}
}
```

`error.code` 使用现有 Go 语义字符串：

- `INVALID_REQUEST`
- `INVALID_ARGUMENT`
- `INVALID_HANDLE`
- `NOT_FOUND`
- `ALREADY_EXISTS`
- `UNSUPPORTED`
- `CONSTRAINT`
- `IO`
- `INTERNAL`

## 类型约定

`describe_table.columns[].type` 使用稳定字符串：

- `BOOLEAN`
- `TINYINT`
- `SMALLINT`
- `INTEGER`
- `BIGINT`
- `FLOAT`
- `DOUBLE`
- `DECIMAL`
- `VARCHAR`

其中 `DECIMAL` 在 v1 中保留但不支持写入。

## Action Examples

### `list_tables`

<!-- example:request:list_tables -->
```json
{
  "version": "v1",
  "request_id": "req-list-001",
  "mode": "read_only",
  "action": "list_tables",
  "args": {}
}
```

<!-- example:response_success:list_tables -->
```json
{
  "ok": true,
  "request_id": "req-list-001",
  "action": "list_tables",
  "data": {
    "tables": [
      "users",
      "orders"
    ]
  },
  "error": null,
  "meta": {}
}
```

<!-- example:response_failure:list_tables -->
```json
{
  "ok": false,
  "request_id": "req-list-001",
  "action": "list_tables",
  "data": null,
  "error": {
    "code": "INTERNAL",
    "message": "database is closed"
  },
  "meta": {}
}
```

### `table_exists`

<!-- example:request:table_exists -->
```json
{
  "version": "v1",
  "request_id": "req-exists-001",
  "mode": "read_only",
  "action": "table_exists",
  "args": {
    "table": "users"
  }
}
```

<!-- example:response_success:table_exists -->
```json
{
  "ok": true,
  "request_id": "req-exists-001",
  "action": "table_exists",
  "data": {
    "table": "users",
    "exists": true
  },
  "error": null,
  "meta": {}
}
```

<!-- example:response_failure:table_exists -->
```json
{
  "ok": false,
  "request_id": "req-exists-001",
  "action": "table_exists",
  "data": null,
  "error": {
    "code": "INTERNAL",
    "message": "database is closed"
  },
  "meta": {}
}
```

### `describe_table`

索引信息当前只暴露 `name`，不伪造 `columns`、`unique` 等底层暂不可得字段。

<!-- example:request:describe_table -->
```json
{
  "version": "v1",
  "request_id": "req-desc-001",
  "mode": "read_only",
  "action": "describe_table",
  "args": {
    "table": "users"
  }
}
```

<!-- example:response_success:describe_table -->
```json
{
  "ok": true,
  "request_id": "req-desc-001",
  "action": "describe_table",
  "data": {
    "table": "users",
    "columns": [
      {
        "name": "id",
        "type": "INTEGER",
        "nullable": false
      },
      {
        "name": "name",
        "type": "VARCHAR",
        "length": 32,
        "nullable": false
      }
    ],
    "indexes": [
      {
        "name": "idx_users_id"
      }
    ]
  },
  "error": null,
  "meta": {}
}
```

<!-- example:response_failure:describe_table -->
```json
{
  "ok": false,
  "request_id": "req-desc-001",
  "action": "describe_table",
  "data": null,
  "error": {
    "code": "NOT_FOUND",
    "message": "table \"users_missing\" not found"
  },
  "meta": {}
}
```

### `get_by_primary_int`

该动作要求目标表首列为 `INTEGER NOT NULL`，并且表上已存在 primary-int index。

<!-- example:request:get_by_primary_int -->
```json
{
  "version": "v1",
  "request_id": "req-get-001",
  "mode": "read_only",
  "action": "get_by_primary_int",
  "args": {
    "table": "users",
    "key": 1
  }
}
```

<!-- example:response_success:get_by_primary_int -->
```json
{
  "ok": true,
  "request_id": "req-get-001",
  "action": "get_by_primary_int",
  "data": {
    "table": "users",
    "row": {
      "id": 1,
      "name": "alice"
    },
    "found": true
  },
  "error": null,
  "meta": {}
}
```

<!-- example:response_failure:get_by_primary_int -->
```json
{
  "ok": false,
  "request_id": "req-get-001",
  "action": "get_by_primary_int",
  "data": null,
  "error": {
    "code": "UNSUPPORTED",
    "message": "table \"users\" requires a primary-int index for this action"
  },
  "meta": {}
}
```

### `scan_all`

`limit` 可选；省略时默认为 `100`。v1 只保留截断语义，不引入分页 token。

<!-- example:request:scan_all -->
```json
{
  "version": "v1",
  "request_id": "req-scan-all-001",
  "mode": "read_only",
  "action": "scan_all",
  "args": {
    "table": "users",
    "limit": 50
  }
}
```

<!-- example:response_success:scan_all -->
```json
{
  "ok": true,
  "request_id": "req-scan-all-001",
  "action": "scan_all",
  "data": {
    "table": "users",
    "rows": [
      {
        "id": 1,
        "name": "alice"
      },
      {
        "id": 2,
        "name": "bob"
      }
    ],
    "row_count": 2,
    "truncated": false
  },
  "error": null,
  "meta": {}
}
```

<!-- example:response_failure:scan_all -->
```json
{
  "ok": false,
  "request_id": "req-scan-all-001",
  "action": "scan_all",
  "data": null,
  "error": {
    "code": "NOT_FOUND",
    "message": "table \"users_missing\" not found"
  },
  "meta": {}
}
```

### `scan_primary_int_range`

该动作要求目标表首列为 `INTEGER NOT NULL`，并且表上已存在 primary-int index。

<!-- example:request:scan_primary_int_range -->
```json
{
  "version": "v1",
  "request_id": "req-range-001",
  "mode": "read_only",
  "action": "scan_primary_int_range",
  "args": {
    "table": "users",
    "start_key": 100,
    "end_key": 200,
    "limit": 25
  }
}
```

<!-- example:response_success:scan_primary_int_range -->
```json
{
  "ok": true,
  "request_id": "req-range-001",
  "action": "scan_primary_int_range",
  "data": {
    "table": "users",
    "rows": [
      {
        "id": 100,
        "name": "alice"
      },
      {
        "id": 101,
        "name": "bob"
      }
    ],
    "row_count": 2,
    "truncated": false
  },
  "error": null,
  "meta": {}
}
```

<!-- example:response_failure:scan_primary_int_range -->
```json
{
  "ok": false,
  "request_id": "req-range-001",
  "action": "scan_primary_int_range",
  "data": null,
  "error": {
    "code": "UNSUPPORTED",
    "message": "table \"users\" requires a primary-int index for this action"
  },
  "meta": {}
}
```

### `insert_row`

`values` 必须覆盖所有已知列，不能包含未知列；`NULL` 和 `DECIMAL` 都不在 v1 支持范围。

<!-- example:request:insert_row -->
```json
{
  "version": "v1",
  "request_id": "req-insert-001",
  "mode": "read_write",
  "action": "insert_row",
  "args": {
    "table": "users",
    "values": {
      "id": 1,
      "name": "alice"
    }
  }
}
```

<!-- example:response_success:insert_row -->
```json
{
  "ok": true,
  "request_id": "req-insert-001",
  "action": "insert_row",
  "data": {
    "table": "users",
    "inserted": 1
  },
  "error": null,
  "meta": {}
}
```

<!-- example:response_failure:insert_row -->
```json
{
  "ok": false,
  "request_id": "req-insert-001",
  "action": "insert_row",
  "data": null,
  "error": {
    "code": "CONSTRAINT",
    "message": "column \"id\": expected integer number"
  },
  "meta": {}
}
```

### `update_by_primary_int`

`values` 允许部分字段 patch，但如果显式提供主键列，它必须等于 `key`。后续 dispatcher 需要先补齐整行，再调用底层 `UpdateRowByPrimaryInt`。

<!-- example:request:update_by_primary_int -->
```json
{
  "version": "v1",
  "request_id": "req-update-001",
  "mode": "read_write",
  "action": "update_by_primary_int",
  "args": {
    "table": "users",
    "key": 1,
    "values": {
      "name": "alice-updated"
    }
  }
}
```

<!-- example:response_success:update_by_primary_int -->
```json
{
  "ok": true,
  "request_id": "req-update-001",
  "action": "update_by_primary_int",
  "data": {
    "table": "users",
    "updated": 1
  },
  "error": null,
  "meta": {}
}
```

<!-- example:response_failure:update_by_primary_int -->
```json
{
  "ok": false,
  "request_id": "req-update-001",
  "action": "update_by_primary_int",
  "data": null,
  "error": {
    "code": "CONSTRAINT",
    "message": "column \"id\" must equal key 1"
  },
  "meta": {}
}
```

### `delete_by_primary_int`

该动作要求目标表首列为 `INTEGER NOT NULL`。

<!-- example:request:delete_by_primary_int -->
```json
{
  "version": "v1",
  "request_id": "req-delete-001",
  "mode": "read_write",
  "action": "delete_by_primary_int",
  "args": {
    "table": "users",
    "key": 1
  }
}
```

<!-- example:response_success:delete_by_primary_int -->
```json
{
  "ok": true,
  "request_id": "req-delete-001",
  "action": "delete_by_primary_int",
  "data": {
    "table": "users",
    "deleted": 1
  },
  "error": null,
  "meta": {}
}
```

<!-- example:response_failure:delete_by_primary_int -->
```json
{
  "ok": false,
  "request_id": "req-delete-001",
  "action": "delete_by_primary_int",
  "data": null,
  "error": {
    "code": "UNSUPPORTED",
    "message": "table \"users\" first column must be INTEGER for primary-int actions"
  },
  "meta": {}
}
```

### `batch`

`batch` 的 `requests` 至少包含一个子动作；子动作结构固定为 `{ "action": "...", "args": {...} }`，并复用单动作的全部参数规则。`batch` 不支持嵌套 `batch`。

<!-- example:request:batch -->
```json
{
  "version": "v1",
  "request_id": "req-batch-001",
  "mode": "read_write",
  "action": "batch",
  "args": {
    "stop_on_error": false,
    "requests": [
      {
        "action": "insert_row",
        "args": {
          "table": "users",
          "values": {
            "id": 1,
            "name": "alice"
          }
        }
      },
      {
        "action": "table_exists",
        "args": {
          "table": "users"
        }
      }
    ]
  }
}
```

<!-- example:response_success:batch -->
```json
{
  "ok": true,
  "request_id": "req-batch-001",
  "action": "batch",
  "data": {
    "results": [
      {
        "index": 0,
        "action": "insert_row",
        "ok": true,
        "data": {
          "table": "users",
          "inserted": 1
        },
        "error": null
      },
      {
        "index": 1,
        "action": "table_exists",
        "ok": true,
        "data": {
          "table": "users",
          "exists": true
        },
        "error": null
      }
    ],
    "total": 2,
    "succeeded": 2,
    "failed": 0,
    "stopped": false
  },
  "error": null,
  "meta": {}
}
```

<!-- example:response_failure:batch -->
```json
{
  "ok": false,
  "request_id": "req-batch-001",
  "action": "batch",
  "data": null,
  "error": {
    "code": "INVALID_REQUEST",
    "message": "requests must contain at least one action"
  },
  "meta": {}
}
```
