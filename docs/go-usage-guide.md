# HaruhiDB Go 使用指南（Web + 网络 + Ollama）

这份文档只覆盖当前保留形态：

- 只保留 `serve` 子命令
- Web UI + HTTP API 并行可用
- NL 翻译固定走 Ollama

## 运行前准备

```bash
cd /home/suzumiya/__code__/code/HaruhiDB/GO
go run ./cmd/haruhidb help
```

一键启动（推荐）：

```bash
# Linux / macOS
cd /home/suzumiya/__code__/code/HaruhiDB
./scripts/start_web_one_click.sh
```

## 当前如何运行（手动）

```bash
cd /path/to/HaruhiDB/GO
go run ./cmd/haruhidb serve --config ../docs/configs/serve-web-ollama.json
```

当前仅支持以下 8 个参数：

- `--config`
- `--db-path`
- `--listen`
- `--model`
- `--base-url`
- `--stream`
- `--timeout`
- `--allow-write`

示例（命令行覆盖配置）：

```bash
go run ./cmd/haruhidb serve \
  --config ../docs/configs/serve-web-ollama.json \
  --db-path ../haruhidb-web.db \
  --listen :8080 \
  --model qwen2.5-coder:3b \
  --base-url http://127.0.0.1:11434 \
  --stream=false \
  --timeout 120s \
  --allow-write=true
```

## 数据库文件位置

- 一键脚本默认使用：`<repo>/haruhidb-web.db`
- 手动配置默认使用：`../haruhidb-web.db`（相对 `GO/`）
- 一键脚本每次运行都会写入 `<repo>/.haruhidb_db_path`，可直接查看当前绝对路径

## HTTP 路由（保留）

- `GET /`：重定向到 `/ui`
- `GET /ui`：Web 页面入口
- `GET /ui/*`：静态资源
- `GET /healthz`：健康检查
- `POST /v1/action`：执行 Action JSON
- `POST /v1/nl/translate`：自然语言翻译

网页流程保持两段式：

1. 调 `/v1/nl/translate` 生成候选 Action
2. 候选通过后调 `/v1/action` 执行

## 配置文件

配置结构仅保留三个分组：

- `common`：`db_path / allow_write / timeout`
- `ollama`：`base_url / model / stream`
- `serve`：`listen`

推荐配置文件：

- `../docs/configs/serve-web-ollama.json`
- `../docs/configs/config-template.json`

## 典型调用

健康检查：

```bash
wget -qO- http://127.0.0.1:8080/healthz
```

Action 执行：

```bash
wget -qO- \
  --method=POST \
  --header='Content-Type: application/json' \
  --body-data='{"version":"v1","request_id":"req-action-1","mode":"read_only","action":"list_tables","args":{}}' \
  http://127.0.0.1:8080/v1/action
```

NL 翻译：

```bash
wget -qO- \
  --method=POST \
  --header='Content-Type: application/json' \
  --body-data='{"request_id":"req-nl-1","input":"列出所有表","mode":"read_only"}' \
  http://127.0.0.1:8080/v1/nl/translate
```

## 兼容性说明

以下 CLI 子命令已移除：

- `run`
- `nl`
- `shell`

调用这些子命令会返回明确错误提示。
