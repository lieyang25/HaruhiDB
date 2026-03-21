# HaruhiDB Action v1 单请求功能演示

这个示例用 **一个** `batch` 请求串起当前 v1 支持的能力，适合作为联调和演示基线。

## 场景前提

- 已存在表 `users(id INTEGER NOT NULL, name VARCHAR(32) NOT NULL)`
- `users` 已建立 primary-int index（例如 `idx_users_id`）
- 预置行：`(1, "alice")`、`(2, "bob")`、`(3, "cindy")`

## 请求（单个 batch）

```json
{
  "version": "v1",
  "request_id": "req-showcase-001",
  "mode": "read_write",
  "action": "batch",
  "args": {
    "stop_on_error": false,
    "requests": [
      {
        "action": "list_tables",
        "args": {}
      },
      {
        "action": "table_exists",
        "args": {
          "table": "users"
        }
      },
      {
        "action": "describe_table",
        "args": {
          "table": "users"
        }
      },
      {
        "action": "scan_all",
        "args": {
          "table": "users",
          "limit": 2
        }
      },
      {
        "action": "scan_primary_int_range",
        "args": {
          "table": "users",
          "start_key": 1,
          "end_key": 3,
          "limit": 10
        }
      },
      {
        "action": "insert_row",
        "args": {
          "table": "users",
          "values": {
            "id": 100,
            "name": "new-user"
          }
        }
      },
      {
        "action": "get_by_primary_int",
        "args": {
          "table": "users",
          "key": 100
        }
      },
      {
        "action": "update_by_primary_int",
        "args": {
          "table": "users",
          "key": 100,
          "values": {
            "name": "new-user-updated"
          }
        }
      },
      {
        "action": "delete_by_primary_int",
        "args": {
          "table": "users",
          "key": 100
        }
      },
      {
        "action": "get_by_primary_int",
        "args": {
          "table": "users",
          "key": 100
        }
      }
    ]
  }
}
```

## 响应（示意）

```json
{
  "ok": true,
  "request_id": "req-showcase-001",
  "action": "batch",
  "data": {
    "results": [
      {
        "index": 0,
        "action": "list_tables",
        "ok": true,
        "data": {
          "tables": [
            "users",
            "orders"
          ]
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
      },
      {
        "index": 2,
        "action": "describe_table",
        "ok": true,
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
        "error": null
      },
      {
        "index": 3,
        "action": "scan_all",
        "ok": true,
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
          "truncated": true
        },
        "error": null
      },
      {
        "index": 4,
        "action": "scan_primary_int_range",
        "ok": true,
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
            },
            {
              "id": 3,
              "name": "cindy"
            }
          ],
          "row_count": 3,
          "truncated": false
        },
        "error": null
      },
      {
        "index": 5,
        "action": "insert_row",
        "ok": true,
        "data": {
          "table": "users",
          "inserted": 1
        },
        "error": null
      },
      {
        "index": 6,
        "action": "get_by_primary_int",
        "ok": true,
        "data": {
          "table": "users",
          "row": {
            "id": 100,
            "name": "new-user"
          },
          "found": true
        },
        "error": null
      },
      {
        "index": 7,
        "action": "update_by_primary_int",
        "ok": true,
        "data": {
          "table": "users",
          "updated": 1
        },
        "error": null
      },
      {
        "index": 8,
        "action": "delete_by_primary_int",
        "ok": true,
        "data": {
          "table": "users",
          "deleted": 1
        },
        "error": null
      },
      {
        "index": 9,
        "action": "get_by_primary_int",
        "ok": true,
        "data": {
          "table": "users",
          "row": null,
          "found": false
        },
        "error": null
      }
    ],
    "total": 10,
    "succeeded": 10,
    "failed": 0,
    "stopped": false
  },
  "error": null,
  "meta": {}
}
```

## 这个示例覆盖了什么

- 顶层 `batch` 执行与结果聚合
- 6 个读动作：`list_tables` / `table_exists` / `describe_table` / `scan_all` / `scan_primary_int_range` / `get_by_primary_int`
- 3 个写动作：`insert_row` / `update_by_primary_int` / `delete_by_primary_int`
- `found=false`、`row=null` 的“未命中”返回语义
