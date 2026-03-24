# HaruhiDB Web 一页上手与动作速查（Web-Only）

这篇是当前唯一主线文档，覆盖：

1. 树莓派部署
2. 本机网页访问
3. API 与动作集
4. 常用请求示例
5. 常见报错排查

## 1. 当前架构（去模型化）

- 服务：HaruhiDB 单进程（内置 UI + API）
- UI：`GET /ui`
- API：`GET /healthz`、`GET /v1/capabilities`、`POST /v1/action`
- 已移除：`POST /v1/nl/translate`

## 2. 树莓派部署（默认方案）

### 2.1 配置文件

编辑 `docs/configs/serve-web-rpi.json`：

```json
{
  "common": {
    "db_path": "/home/pi/haruhidb/data/main.db",
    "allow_write": true,
    "timeout": "30s"
  },
  "serve": {
    "listen": "0.0.0.0:8080"
  }
}
```

### 2.2 启动

```bash
cd /home/pi/HaruhiDB
HARU_CONFIG=docs/configs/serve-web-rpi.json HARU_OPEN_BROWSER=false ./scripts/start_web_one_click.sh
```

### 2.3 验证

```bash
curl -s http://127.0.0.1:8080/healthz
curl -s "http://127.0.0.1:8080/v1/capabilities"
```

本机浏览器访问：`http://<pi-ip>:8080/ui`

## 3. API 速查

- `GET /healthz`：服务健康检查
- `GET /v1/capabilities?db_path=<optional>`：返回协议版本、动作清单、按 mode 分类、当前表清单
- `POST /v1/action`：执行 Action envelope（支持可选 `db_path` 外层字段）

## 4. 支持动作表（14 个）

| 动作 | 分类 | mode | 必填 args | 说明 |
| --- | --- | --- | --- | --- |
| `list_tables` | metadata | `read_only/read_write` | 无 | 列出所有表 |
| `table_exists` | metadata | `read_only/read_write` | `table` | 判断表是否存在 |
| `describe_table` | metadata | `read_only/read_write` | `table` | 查看表结构与索引 |
| `get_by_primary_int` | read | `read_only/read_write` | `table`,`key` | 按主键取一行 |
| `scan_all` | read | `read_only/read_write` | `table` | 全表扫描（可选 `limit`） |
| `scan_primary_int_range` | read | `read_only/read_write` | `table`,`start_key`,`end_key` | 主键范围扫描（可选 `limit`） |
| `insert_row` | write | `read_write` | `table`,`values` | 插入一行 |
| `update_by_primary_int` | write | `read_write` | `table`,`key`,`values` | 按主键更新 |
| `delete_by_primary_int` | write | `read_write` | `table`,`key` | 按主键删除 |
| `create_table` | ddl | `read_write` | `table`,`columns` | 建表 |
| `drop_table` | ddl | `read_write` | `table` | 删表 |
| `create_primary_int_index` | ddl | `read_write` | `table`,`index` | 创建主键整型索引 |
| `drop_index` | ddl | `read_write` | `table`,`index` | 删除索引 |
| `batch` | batch | `read_only/read_write` | `requests` | 顺序执行多个子动作（可选 `stop_on_error`） |

## 5. Action 示例

### 5.1 列出所有表

```bash
curl -s http://127.0.0.1:8080/v1/action \
  -H 'Content-Type: application/json' \
  -d '{
    "version":"v3",
    "request_id":"req-list-1",
    "mode":"read_only",
    "action":"list_tables",
    "args":{}
  }'
```

### 5.2 建表 + 建索引（batch）

```bash
curl -s http://127.0.0.1:8080/v1/action \
  -H 'Content-Type: application/json' \
  -d '{
    "version":"v3",
    "request_id":"req-init-1",
    "mode":"read_write",
    "action":"batch",
    "args":{
      "stop_on_error": true,
      "requests":[
        {
          "action":"create_table",
          "args":{
            "table":"characters",
            "columns":[
              {"name":"id","type":"INTEGER","nullable":false},
              {"name":"name","type":"VARCHAR","length":64,"nullable":false}
            ]
          }
        },
        {
          "action":"create_primary_int_index",
          "args":{"table":"characters","index":"idx_characters_id"}
        }
      ]
    }
  }'
```

### 5.3 插入 + 查询

```bash
curl -s http://127.0.0.1:8080/v1/action \
  -H 'Content-Type: application/json' \
  -d '{
    "version":"v3",
    "request_id":"req-insert-1",
    "mode":"read_write",
    "action":"insert_row",
    "args":{"table":"characters","values":{"id":1,"name":"凉宫春日"}}
  }'

curl -s http://127.0.0.1:8080/v1/action \
  -H 'Content-Type: application/json' \
  -d '{
    "version":"v3",
    "request_id":"req-get-1",
    "mode":"read_only",
    "action":"get_by_primary_int",
    "args":{"table":"characters","key":1}
  }'
```

## 6. Web 控制台操作顺序

1. 打开 `/ui`
2. 在“数据库上下文”里切换 `db_path`
3. 等待 capabilities 加载完成
4. 用快捷动作或 Action JSON 控制台执行
5. 写动作建议使用 `read_write`，读动作用 `read_only`
6. 复杂流程直接用 `batch`

## 7. 常见问题

1. `unsupported action ...`
- 动作名不在 14 个支持动作里，或拼写错误。

2. `write actions are disabled by server configuration`
- 配置里 `common.allow_write=false`，改为 `true` 后重启。

3. `context deadline exceeded`
- 请求超时，增大 `common.timeout`（例如 `30s -> 60s`），然后重启服务。

4. `/v1/nl/translate` 404
- 这是预期行为：该接口已移除。
