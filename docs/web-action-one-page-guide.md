# HaruhiDB 一页上手与动作速查（Web + 远程 Ollama）

这篇文档用于解决两个问题：

1. 不熟流程，不知道先做什么
2. 不确定哪些动作受支持、该怎么写

适用场景：本机运行 HaruhiDB 服务，通过树莓派 Ollama 返回结构化动作，再由本机执行。

## 1) 先跑起来（最短路径）

### 1.1 编辑配置

推荐直接编辑：

- `docs/configs/serve-web-ollama-rpi.json`

关键字段：

```json
{
  "ollama": {
    "base_url": "http://192.168.137.236:11434",
    "model": "qwen2.5-coder:0.5b",
    "stream": false
  }
}
```

如果你在树莓派上换了模型名，只改 `model` 即可。

### 1.2 启动服务（脚本）

```bash
cd /home/suzumiya/__code__/code/HaruhiDB
HARU_CONFIG=docs/configs/serve-web-ollama-rpi.json HARU_OPEN_BROWSER=false ./scripts/start_web_one_click.sh
```

### 1.3 验证链路

```bash
curl -s http://127.0.0.1:8080/healthz
curl -s http://127.0.0.1:8080/v1/nl/translate \
  -H 'Content-Type: application/json' \
  -d '{"request_id":"req-check-1","input":"列出所有表","mode":"read_only"}'
```

只要翻译返回 `valid=true`，就表示“树莓派推理 -> 本机拿结构”已打通。

## 2) 协议速记

- 顶层信封固定 5 个字段：`version` / `request_id` / `mode` / `action` / `args`
- `mode` 只允许：`read_only`、`read_write`
- 只允许固定动作名，不允许自造动作（例如 `insert_table` 是非法动作）
- `args` 必须是 JSON 对象，拒绝未知字段

最小模板：

```json
{
  "version": "v3",
  "request_id": "req-0001",
  "mode": "read_only",
  "action": "list_tables",
  "args": {}
}
```

版本说明：

- 当前服务接受 `v1` / `v2` / `v3`
- 服务端会统一按 `v3` 语义处理（canonicalize to v3）
- 新请求建议直接用 `v3`

## 3) 支持动作总表（当前实现）

也可以通过接口实时查看（推荐）：

```bash
curl -s 'http://127.0.0.1:8080/v1/capabilities' | jq '.actions[].name'
```

| 动作 | 分类 | 支持 mode | 必填参数 | 说明 |
| --- | --- | --- | --- | --- |
| `list_tables` | metadata | `read_only`, `read_write` | 无 | 列出所有表 |
| `table_exists` | metadata | `read_only`, `read_write` | `table` | 判断表是否存在 |
| `describe_table` | metadata | `read_only`, `read_write` | `table` | 查看列与索引 |
| `get_by_primary_int` | read | `read_only`, `read_write` | `table`, `key` | 按主键整型查一行 |
| `scan_all` | read | `read_only`, `read_write` | `table` | 全表扫描（可加 `limit`） |
| `scan_primary_int_range` | read | `read_only`, `read_write` | `table`, `start_key`, `end_key` | 主键范围扫描（可加 `limit`） |
| `insert_row` | write | `read_write` | `table`, `values` | 插入一行 |
| `update_by_primary_int` | write | `read_write` | `table`, `key`, `values` | 按主键更新 |
| `delete_by_primary_int` | write | `read_write` | `table`, `key` | 按主键删除 |
| `create_table` | ddl | `read_write` | `table`, `columns` | 建表 |
| `drop_table` | ddl | `read_write` | `table` | 删表 |
| `create_primary_int_index` | ddl | `read_write` | `table`, `index` | 建主键整型索引 |
| `drop_index` | ddl | `read_write` | `table`, `index` | 删索引 |
| `batch` | batch | `read_only`, `read_write` | `requests` | 顺序执行子动作 |

## 4) 每个动作的最小示例（可复制）

### `list_tables`

```json
{"version":"v3","request_id":"req-list","mode":"read_only","action":"list_tables","args":{}}
```

### `table_exists`

```json
{"version":"v3","request_id":"req-exists","mode":"read_only","action":"table_exists","args":{"table":"roles"}}
```

### `describe_table`

```json
{"version":"v3","request_id":"req-desc","mode":"read_only","action":"describe_table","args":{"table":"roles"}}
```

### `get_by_primary_int`

```json
{"version":"v3","request_id":"req-get","mode":"read_only","action":"get_by_primary_int","args":{"table":"roles","key":1}}
```

### `scan_all`

```json
{"version":"v3","request_id":"req-scan-all","mode":"read_only","action":"scan_all","args":{"table":"roles","limit":20}}
```

### `scan_primary_int_range`

```json
{"version":"v3","request_id":"req-range","mode":"read_only","action":"scan_primary_int_range","args":{"table":"roles","start_key":1,"end_key":100,"limit":20}}
```

### `insert_row`

```json
{"version":"v3","request_id":"req-insert","mode":"read_write","action":"insert_row","args":{"table":"roles","values":{"id":1,"name":"凉宫春日","gender":"女"}}}
```

### `update_by_primary_int`

```json
{"version":"v3","request_id":"req-update","mode":"read_write","action":"update_by_primary_int","args":{"table":"roles","key":1,"values":{"name":"凉宫春日(更新版)"}}}
```

### `delete_by_primary_int`

```json
{"version":"v3","request_id":"req-delete","mode":"read_write","action":"delete_by_primary_int","args":{"table":"roles","key":1}}
```

### `create_table`

```json
{"version":"v3","request_id":"req-create-table","mode":"read_write","action":"create_table","args":{"table":"roles","columns":[{"name":"id","type":"INTEGER","nullable":false},{"name":"name","type":"VARCHAR","length":64,"nullable":false},{"name":"gender","type":"VARCHAR","length":16,"nullable":false}]}}
```

### `drop_table`

```json
{"version":"v3","request_id":"req-drop-table","mode":"read_write","action":"drop_table","args":{"table":"roles"}}
```

### `create_primary_int_index`

```json
{"version":"v3","request_id":"req-create-index","mode":"read_write","action":"create_primary_int_index","args":{"table":"roles","index":"idx_roles_id"}}
```

### `drop_index`

```json
{"version":"v3","request_id":"req-drop-index","mode":"read_write","action":"drop_index","args":{"table":"roles","index":"idx_roles_id"}}
```

### `batch`

```json
{
  "version": "v3",
  "request_id": "req-batch-1",
  "mode": "read_write",
  "action": "batch",
  "args": {
    "stop_on_error": true,
    "requests": [
      {"action":"insert_row","args":{"table":"roles","values":{"id":1,"name":"凉宫春日","gender":"女"}}},
      {"action":"get_by_primary_int","args":{"table":"roles","key":1}},
      {"action":"delete_by_primary_int","args":{"table":"roles","key":1}}
    ]
  }
}
```

## 5) Web UI 推荐操作顺序

1. `Mode` 按需求选择：读操作用 `read_only`，写操作用 `read_write`
2. 先点“仅翻译”，确认 `valid=true` 且 `candidate_envelope.action` 符合预期
3. 再点“翻译并执行”

## 6) 常见报错与修复

### 6.1 `unsupported action "insert_table"`

原因：模型生成了不支持的动作名（幻觉）。

修复：

1. 把提示词写成“严格动作名”格式，例如：

```text
严格只执行 1 步：
insert_row(table=roles, values={id:1,name:'凉宫春日',gender:'女'})。
禁止输出任何额外动作，禁止使用 insert_table。
```

2. 对写入操作必须选 `mode=read_write`
3. 若仍不稳，优先换成更大的模型（例如 `qwen2.5-coder:3b`）

### 6.2 `table "xxx" not found`

原因：当前数据库里没有目标表。

修复：

1. 先 `create_table`
2. 再 `create_primary_int_index`（若后续用 `get_by_primary_int` / `scan_primary_int_range`）
3. 再执行读写动作

## 7) 相关阅读（按需）

- 协议细节与完整示例：`docs/action-protocol-v1.md`
- 网页原理与数据流：`docs/web-principle-and-dataflow.md`
- 排错经验：`docs/error-experience-playbook.md`
- 配置项总表：`docs/config-parameter-reference.md`
