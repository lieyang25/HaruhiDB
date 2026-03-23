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
3. 先点“仅翻译”，确认 `candidate_envelope.version = "v2"`
4. 再点“翻译并执行”，观察 `batch.results` 全部通过

## 可直接粘贴的 NL 提示词（严格版）

```text
严格输出 v2 batch。按顺序执行 7 步：
1) create_table(table=books_v2_demo, columns=[{name:id,type:INTEGER,nullable:false},{name:name,type:VARCHAR,length:32,nullable:false}])；
2) create_primary_int_index(table=books_v2_demo,index=idx_books_v2_demo_id)；
3) insert_row(table=books_v2_demo,values={id:501,name:'book-demo'})；
4) get_by_primary_int(table=books_v2_demo,key=501)；
5) delete_by_primary_int(table=books_v2_demo,key=501)；
6) drop_index(table=books_v2_demo,index=idx_books_v2_demo_id)；
7) drop_table(table=books_v2_demo)。
禁止输出任何额外动作。
```
