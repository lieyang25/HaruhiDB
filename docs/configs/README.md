# HaruhiDB 启动配置（Web + Ollama）

当前 `docs/configs` 只保留两类配置文件：

- `serve-web-ollama.json`：可直接用于 Web 服务启动的推荐配置
- `serve-web-ollama-rpi.json`：本地服务 + 树莓派 Ollama 的示例配置
- `config-template.json`：最小模板，适合复制后按环境修改

## 配置结构

配置只保留三个顶层分组：

- `common`
  - `db_path`：数据库文件路径
  - `allow_write`：是否允许写动作（默认建议 `true`）
  - `timeout`：请求超时（Go duration）
- `ollama`
  - `base_url`：Ollama 地址（默认 `http://127.0.0.1:11434`）
  - `model`：模型名（默认 `qwen2.5-coder:3b`）
  - `stream`：是否启用流式翻译（默认建议 `true`）
- `serve`
  - `listen`：HTTP 监听地址

## 启动示例

```bash
cd /home/suzumiya/__code__/code/HaruhiDB/GO
go run ./cmd/haruhidb serve --config ../docs/configs/serve-web-ollama.json
```

命令行参数会覆盖配置文件同名项，例如：

```bash
go run ./cmd/haruhidb serve \
  --config ../docs/configs/serve-web-ollama.json \
  --db-path /tmp/haruhidb-demo.db \
  --model qwen2.5-coder:3b \
  --listen :8090
```

也可以使用 `HARUHIDB_CONFIG`：

```bash
export HARUHIDB_CONFIG=../docs/configs/serve-web-ollama.json
go run ./cmd/haruhidb serve
```

## 一键脚本使用指定配置

`scripts/start_web_one_click.sh` 支持 `HARU_CONFIG` 覆盖默认配置文件：

```bash
cd /home/suzumiya/__code__/code/HaruhiDB
HARU_CONFIG=docs/configs/serve-web-ollama-rpi.json ./scripts/start_web_one_click.sh
```
