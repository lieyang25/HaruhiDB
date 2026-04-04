# HaruhiDB Action v2 单请求功能演示

这个示例给你一个可直接用于网页或 CLI 的 `v2` 动作集示意。

## 场景前提

- 建议使用“已有数据页”的数据库文件进行验证（例如 `quickstart_demo.db` 的拷贝）。
- `mode` 需要 `read_write`。

## 请求（单个 batch）

```json
{
  "version": "v2",
  "request_id": "req-v2-showcase-001",
  "mode": "read_write",
  "action": "batch",
  "args": {
    "stop_on_error": true,
    "requests": [
      {
        "action": "create_table",
        "args": {
          "table": "books_v2_demo",
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
          ]
        }
      },
      {
        "action": "create_primary_int_index",
        "args": {
          "table": "books_v2_demo",
          "index": "idx_books_v2_demo_id"
        }
      },
      {
        "action": "insert_row",
        "args": {
          "table": "books_v2_demo",
          "values": {
            "id": 501,
            "name": "book-demo"
          }
        }
      },
      {
        "action": "get_by_primary_int",
        "args": {
          "table": "books_v2_demo",
          "key": 501
        }
      },
      {
        "action": "delete_by_primary_int",
        "args": {
          "table": "books_v2_demo",
          "key": 501
        }
      },
      {
        "action": "drop_index",
        "args": {
          "table": "books_v2_demo",
          "index": "idx_books_v2_demo_id"
        }
      },
      {
        "action": "drop_table",
        "args": {
          "table": "books_v2_demo"
        }
      }
    ]
  }
}
```

## 响应关键检查点

- 第 0 步 `create_table`：`ok=true`
- 第 1 步 `create_primary_int_index`：`ok=true`
- 第 3 步 `get_by_primary_int`：`found=true`
- 第 6 步 `drop_table`：`ok=true`

## 网页测试方法

1. 打开 Web Console：`http://127.0.0.1:8080/ui`
2. `Mode` 选 `read_write`
3. 在 Action JSON 控制台粘贴上面的 `v2` 请求
4. 点击执行，确认返回的 `batch.results` 全部通过
