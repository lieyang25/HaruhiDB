# HaruhiDB Go 使用指南（本地 / 网络 / 模型组合）

这份文档专门回答一个问题：

- 怎么跑起来？
- 哪些场景需要模型？
- 本地与网络服务分别怎么用？

## 先理解两条链路

HaruhiDB Go 层有两条独立链路：

1. Action 执行链路（不需要模型）
- 输入：Action JSON（`version/mode/action/args`）
- 入口：`run` 子命令、`/v1/action`、`shell` 的 `:json`
- 用途：直接执行数据库动作

2. NL 翻译链路（需要 translator，也就是模型后端）
- 输入：自然语言
- 入口：`nl` 子命令、`/v1/nl/translate`、`shell` 的 `:nl`
- 用途：先把自然语言翻译成 Action JSON，再可选执行

结论：
- 你问到的 “`shell` 可启动，但 `:nl` 会失败”，并不表示本地不支持模型。
- 真正含义是：当前没配置 translator（没提供 `--openai-api-key` 或 `OPENAI_API_KEY`）。

## 是否必须先启动项目服务器

不一定，取决于你走哪条链路：

1. 走 CLI 子命令（`run` / `nl` / `shell`）
- 不需要先启动 `serve`
- `go run ./cmd/haruhidb nl ...` 会在当前进程内直接执行翻译和动作

2. 走 HTTP 接口（`/v1/action` / `/v1/nl/translate`）
- 必须先启动 `serve`
- 其他组件通过 HTTP 调用时，才需要这个模式

## 运行前准备

在 Go 模块目录执行命令：

```bash
cd /home/suzumiya/__code__/code/HaruhiDB/GO
```

查看总帮助：

```bash
go run ./cmd/haruhidb help
```

如果你只想一键跑起来（推荐）：

```bash
cd /home/suzumiya/__code__/code/HaruhiDB
./scripts/serve_ollama_one_click.sh
```

可选环境变量（不改脚本直接调参数）：

- `HARU_MODEL`：默认 `qwen2.5-coder:0.5b`
- `HARU_DB_PATH`：默认 `/tmp/haruhidb-ollama.db`
- `HARU_LISTEN`：默认 `:8080`
- `HARU_TIMEOUT`：默认 `60s`

示例：

```bash
HARU_MODEL=qwen2.5-coder:1.5b HARU_LISTEN=:9090 ./scripts/serve_ollama_one_click.sh
```

常用快捷参数（与原参数等价）：

- `--api-key` = `--openai-api-key`
- `--base-url` = `--openai-base-url`
- `--model` = `--openai-model`
- `--reasoning-effort`：思考强度开关（`off` / `low` / `medium` / `high`）
- `--examples-path`：把本地范例文档注入 NL 翻译提示词
- `--ollama`：一键使用本地 Ollama（默认 `http://127.0.0.1:11434` + `qwen2.5-coder:0.5b`）

## 配置文件启动（推荐）

现在支持通过 `--config`（或环境变量 `HARUHIDB_CONFIG`）读取 JSON 配置文件。

优先级规则：

1. 命令行参数（最高）
2. 配置文件
3. 程序默认值

示例：

```bash
go run ./cmd/haruhidb serve --config ../docs/configs/serve-no-llm.json
go run ./cmd/haruhidb serve --config ../docs/configs/serve-openai.json
go run ./cmd/haruhidb serve --config ../docs/configs/serve-ollama.json
```

也可以用环境变量：

```bash
export HARUHIDB_CONFIG=../docs/configs/serve-ollama.json
go run ./cmd/haruhidb serve
```

命令行覆盖配置示例：

```bash
go run ./cmd/haruhidb serve \
  --config ../docs/configs/serve-ollama.json \
  --listen :9090 \
  --model qwen2.5-coder:1.5b
```

配置文件主要字段：

- `common.db_path`：数据库路径
- `common.allow_write`：是否允许写动作
- `common.timeout`：请求超时，格式同 Go duration（如 `15s`、`1m`）
- `llm.backend`：`none` / `openai` / `openai_compatible` / `ollama`
- `llm.ollama`：是否启用 Ollama 快捷模式（等价 `--ollama`）
- `llm.stream`：是否启用流式翻译（对长思考模型更稳）
- `llm.api_key`：模型服务 key（可选）
- `llm.base_url`：模型服务地址
- `llm.model`：模型名
- `llm.ollama_model`：配合 ollama 的模型名快捷字段
- `llm.reasoning_effort`：思考强度（`off` / `low` / `medium` / `high`）
- `llm.examples_path`：范例文档路径，会注入到 NL 提示词
- `serve.listen`、`serve.max_body_bytes`、`serve.auth_token`、`serve.rate_limit_per_minute`、`serve.trust_proxy_headers`
- `nl.mode`、`nl.execute`、`nl.pretty`
- `run.pretty`
- `shell.mode`

## 模型思考模式开关（reasoning_effort）

如果你使用支持该参数的模型（例如 OpenAI 推理模型），可以通过 `reasoning_effort` 控制“思考/不思考”：

- `off`：尽量关闭思考过程，响应更快、更省
- `low` / `medium` / `high`：逐步增加思考强度

配置文件写法：

```json
{
  "llm": {
    "backend": "openai",
    "model": "gpt-5-mini",
    "reasoning_effort": "off"
  }
}
```

命令行写法：

```bash
go run ./cmd/haruhidb nl \
  --db-path /tmp/haru.db \
  --input "列出所有表" \
  --openai-model gpt-5-mini \
  --reasoning-effort off
```

## 让模型读取范例（examples_path）

你可以把协议示例文件注入模型提示词，帮助小模型稳定输出：

配置文件写法：

```json
{
  "llm": {
    "backend": "openai",
    "model": "gpt-5-mini",
    "examples_path": "../docs/action-v1-showcase-example.md"
  }
}
```

命令行覆盖写法：

```bash
go run ./cmd/haruhidb nl \
  --config ../docs/configs/serve-openai.json \
  --input "先插入用户再检查是否存在" \
  --examples-path ../docs/action-v1-showcase-example.md
```

说明：
- `examples_path` 按“程序运行时当前工作目录”解析相对路径
- 文件过长会自动截断，只保留前半部分用于提示词

## 组合总表

1. 本地 CLI，不用模型
- 可用：`run`、`shell(:json)`
- 不可用：`nl`、`shell(:nl)`

2. 本地 CLI，用网络模型（OpenAI）
- 可用：`nl`、`shell(:nl)`
- 配置：`OPENAI_API_KEY` 或 `--openai-api-key`

3. 本地 CLI，用本地模型（Ollama/OpenAI 兼容）
- 可用：`nl`、`shell(:nl)`
- 配置：推荐直接 `--ollama`（可选 `--ollama-model`）

4. 网络服务（HTTP），不用模型
- 可用：`GET /healthz`、`POST /v1/action`
- `POST /v1/nl/translate` 会返回 translator 未配置

5. 网络服务（HTTP），用网络模型（OpenAI）
- 可用：`POST /v1/nl/translate`
- 服务端会请求 OpenAI `chat/completions`

6. 网络服务（HTTP），用本地模型（Ollama/OpenAI 兼容）
- 可用：`POST /v1/nl/translate`
- 服务端请求你的本地 OpenAI 兼容地址

## 场景命令（可直接复制）

### A. 本地 CLI，不用模型（推荐入门第一步）

```bash
go run ./cmd/haruhidb run \
  --db-path /tmp/haru.db \
  --json '{"version":"v1","request_id":"r1","mode":"read_only","action":"list_tables","args":{}}'
```

### B. 本地 CLI，用网络模型（OpenAI）

```bash
export OPENAI_API_KEY=your_key

go run ./cmd/haruhidb nl \
  --db-path /tmp/haru.db \
  --input "列出所有表" \
  --openai-model gpt-5-mini
```

### C. 本地 CLI，用本地模型（Ollama）

先确保本地模型服务已启动并已拉取模型，例如：

```bash
ollama run qwen2.5-coder:0.5b
```

然后：

```bash
go run ./cmd/haruhidb nl \
  --db-path /tmp/haru.db \
  --input "列出所有表" \
  --ollama
```

如果你想换模型：

```bash
go run ./cmd/haruhidb nl \
  --db-path /tmp/haru.db \
  --input "列出所有表" \
  --ollama \
  --ollama-model qwen2.5-coder:1.5b
```

说明：
- `--ollama` 会自动设置 `--openai-base-url=http://127.0.0.1:11434`
- 默认模型是 `qwen2.5-coder:0.5b`
- 也可手工传 `--openai-base-url` 与 `--openai-model`
- `--openai-base-url` 不要带 `/v1`，程序会自动拼接 `/v1/chat/completions`

### D. 网络服务，不用模型

启动服务：

```bash
go run ./cmd/haruhidb serve \
  --db-path /tmp/haru.db \
  --listen :8080
```

客户端调用 Action：

```bash
wget -qO- \
  --method=POST \
  --header='Content-Type: application/json' \
  --body-data='{"version":"v1","request_id":"req-action-1","mode":"read_only","action":"list_tables","args":{}}' \
  http://127.0.0.1:8080/v1/action
```

### E. 网络服务，用网络模型（OpenAI）

```bash
export OPENAI_API_KEY=your_key

go run ./cmd/haruhidb serve \
  --db-path /tmp/haru.db \
  --listen :8080 \
  --openai-model gpt-5-mini
```

客户端调用 NL 翻译：

```bash
wget -qO- \
  --method=POST \
  --header='Content-Type: application/json' \
  --body-data='{"request_id":"req-nl-1","input":"列出所有表","mode":"read_only"}' \
  http://127.0.0.1:8080/v1/nl/translate
```

### F. 网络服务，用本地模型（Ollama）

```bash
go run ./cmd/haruhidb serve \
  --db-path /tmp/haru.db \
  --listen :8080 \
  --ollama
```

然后客户端继续调用 `/v1/nl/translate`。

## 模型直连快速试跑（插入 / 查询 / 删除）

目标：只用自然语言输入，让模型翻译并直接执行（不手写 JSON）。

先准备一个已有表的数据文件（示例里用 `student` 表）：

```bash
cd /home/suzumiya/__code__/code/HaruhiDB
cp GO/haruhidb/test_output/quickstart_demo.db /tmp/haruhidb-nl-quicktest.db
cp GO/haruhidb/test_output/quickstart_demo.wal /tmp/haruhidb-nl-quicktest.wal || true
```

然后进入 Go 目录，用预置配置直接跑：

```bash
cd /home/suzumiya/__code__/code/HaruhiDB/GO

# OpenAI
export OPENAI_API_KEY=your_key
go run ./cmd/haruhidb nl \
  --config ../docs/configs/nl-quicktest-openai.json

# Ollama
# 先确认本地 Ollama 服务可用，且模型已拉取 qwen3:1.7b
ollama pull qwen3:1.7b

# 携带思考参数（reasoning_effort=medium）
go run ./cmd/haruhidb nl \
  --config ../docs/configs/nl-quicktest-ollama.json

# 不携带思考参数（reasoning_effort=off）
go run ./cmd/haruhidb nl \
  --config ../docs/configs/nl-quicktest-ollama-no-thinking.json

# 需要时可临时覆盖配置里的 nl.input
go run ./cmd/haruhidb nl \
  --config ../docs/configs/nl-quicktest-ollama.json \
  --input "查询 student 表主键 id=1 的记录"
```

说明：

- 这两份 `nl-quicktest-*.json` 已包含 `llm.examples_path`，会先注入动作范例再翻译。
- 这两份配置也已开启 `common.allow_write=true` + `nl.execute=true`，翻译通过会直接执行。
- 当前 Action 协议不含“建表动作”，所以快速测试依赖已有表（示例用 `student`）。
- `nl-quicktest-ollama.json` 使用 `qwen3:1.7b` 且 `reasoning_effort=medium`。
- `nl-quicktest-ollama-no-thinking.json` 使用 `qwen3:1.7b` 且 `reasoning_effort=off`。
- 这两份 qwen3 配置默认开启 `llm.stream=true`，用于缓解非流式长时间不回包。
- 现在 `nl.input` / `nl.input_file`、`run.input` / `run.json` 都已支持配置化。
- 你也可以直接给一段组合话术（让模型一次生成 `batch`），例如：

```bash
go run ./cmd/haruhidb nl \
  --config ../docs/configs/nl-quicktest-openai.json \
  --input "请在 student 表按顺序执行：插入 id=103, name='sora'；查询主键 103；删除主键 103；再次查询主键 103 确认不存在。"
```

## shell 模式的真实行为

启动：

```bash
go run ./cmd/haruhidb shell --db-path /tmp/haru.db --ollama
```

1. `:json <payload>`
- 不需要模型
- 永远走 Action 执行链路

2. `:nl <text>`
- 需要模型（translator）
- 无 translator 时会返回：`translator is not configured`

3. `:status`
- 查看当前 shell 的默认 mode 和 NL 可用状态

## 常见报错与定位

1. `translator is not configured`
- 原因：你走了 NL 链路，但没有配置 `--openai-api-key`/`OPENAI_API_KEY`
- 处理：补上 key（OpenAI）或直接使用 `--ollama`

2. `.../v1/chat/completions ... connection refused`
- 原因：模型后端地址不通（服务未启动、端口错误）
- 处理：检查 `--openai-base-url`、模型服务状态、端口

3. `translator is required; provide OPENAI_API_KEY, --openai-api-key, or use --ollama`
- 原因：使用 `nl` 子命令时强制要求 translator
- 处理：加 key 或环境变量

4. 写请求被拒绝
- 现象：提示 write actions disabled
- 原因：服务/命令未加 `--allow-write`
- 处理：明确需要写操作时增加 `--allow-write`

## 推荐学习路径（从易到难）

1. 先用 `run --json` 跑通 `list_tables`（确认数据库与 Action 协议）
2. 再用 `serve` + `/v1/action`（确认网络服务链路）
3. 最后接 `nl` 或 `/v1/nl/translate`（确认模型翻译链路）
4. 全部稳定后，再启用 `--allow-write` 做写操作联调
