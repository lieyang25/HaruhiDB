# HaruhiDB 配置参数总表（Go）

这份文档集中列出当前 Go 侧所有可用配置键，并标注对应命令行参数。

## 配置优先级

1. 命令行参数（最高）
2. 配置文件（`--config` 或 `HARUHIDB_CONFIG`）
3. 程序默认值

## `common`（所有子命令通用）

| 配置键 | 类型 | 说明 | CLI 参数 |
| --- | --- | --- | --- |
| `common.db_path` | string | 数据库文件路径 | `--db-path` |
| `common.allow_write` | bool | 允许写动作 | `--allow-write` |
| `common.timeout` | string | 请求超时（Go duration） | `--timeout` |

## `llm`（NL 翻译相关）

| 配置键 | 类型 | 说明 | CLI 参数 |
| --- | --- | --- | --- |
| `llm.backend` | string | `none/openai/openai_compatible/ollama` | `--llm-backend` |
| `llm.ollama` | bool | 是否启用 Ollama 快捷模式 | `--ollama` |
| `llm.stream` | bool | 是否启用流式翻译响应 | `--stream` |
| `llm.api_key` | string | OpenAI API Key（也可用环境变量） | `--openai-api-key` / `--api-key` |
| `llm.base_url` | string | OpenAI 兼容服务地址 | `--openai-base-url` / `--base-url` |
| `llm.model` | string | 模型名 | `--openai-model` / `--model` |
| `llm.ollama_model` | string | Ollama 快捷模型名 | `--ollama-model` |
| `llm.reasoning_effort` | string | 思考强度：`off/low/medium/high` | `--reasoning-effort` |
| `llm.examples_path` | string | 范例文档路径（注入到 NL 提示词） | `--examples-path` |

## `serve`（HTTP 服务）

| 配置键 | 类型 | 说明 | CLI 参数 |
| --- | --- | --- | --- |
| `serve.listen` | string | 监听地址 | `--listen` |
| `serve.max_body_bytes` | int64 | 请求体上限 | `--max-body-bytes` |
| `serve.auth_token` | string | Bearer 鉴权 token | `--auth-token` |
| `serve.rate_limit_per_minute` | int | 每客户端分钟限流 | `--rate-limit-per-minute` |
| `serve.trust_proxy_headers` | bool | 是否信任 `X-Forwarded-For/X-Real-IP` | `--trust-proxy-headers` |

## `run`

| 配置键 | 类型 | 说明 | CLI 参数 |
| --- | --- | --- | --- |
| `run.input` | string | JSON 请求文件路径（`-` 表示 stdin） | `--input` |
| `run.json` | string | 内联 JSON 请求 | `--json` |
| `run.pretty` | bool | 是否美化输出 | `--pretty` |

## `nl`

| 配置键 | 类型 | 说明 | CLI 参数 |
| --- | --- | --- | --- |
| `nl.input` | string | 自然语言输入文本 | `--input` |
| `nl.input_file` | string | 自然语言输入文件（`-` 表示 stdin） | `--input-file` |
| `nl.mode` | string | `read_only/read_write` | `--mode` |
| `nl.execute` | bool | 翻译成功后是否直接执行 | `--execute` |
| `nl.pretty` | bool | 是否美化输出 | `--pretty` |

## `shell`

| 配置键 | 类型 | 说明 | CLI 参数 |
| --- | --- | --- | --- |
| `shell.mode` | string | shell 下 `:nl` 默认 mode | `--mode` |

## 推荐配置化示例

- 完整示例文件：[`docs/configs/config-full-example.json`](configs/config-full-example.json)
- OpenAI 示例：[`docs/configs/serve-openai.json`](configs/serve-openai.json)
- Ollama 示例：[`docs/configs/serve-ollama.json`](configs/serve-ollama.json)
- 无模型示例：[`docs/configs/serve-no-llm.json`](configs/serve-no-llm.json)
