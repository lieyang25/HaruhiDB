# HaruhiDB 配置参数总表（Go / Web + Ollama）

这份文档列出当前 Go `serve` 模式的全部配置键与命令行参数映射。

## 配置优先级

1. 命令行参数（最高）
2. 配置文件（`--config` 或 `HARUHIDB_CONFIG`）
3. 程序默认值

## `common`

| 配置键 | 类型 | 说明 | CLI 参数 |
| --- | --- | --- | --- |
| `common.db_path` | string | 数据库文件路径 | `--db-path` |
| `common.allow_write` | bool | 是否允许写动作 | `--allow-write` |
| `common.timeout` | string | 请求超时（Go duration） | `--timeout` |

## `ollama`

| 配置键 | 类型 | 说明 | CLI 参数 |
| --- | --- | --- | --- |
| `ollama.base_url` | string | Ollama 地址 | `--base-url` |
| `ollama.model` | string | Ollama 模型名 | `--model` |
| `ollama.stream` | bool | 是否启用流式翻译 | `--stream` |

## `serve`

| 配置键 | 类型 | 说明 | CLI 参数 |
| --- | --- | --- | --- |
| `serve.listen` | string | HTTP 监听地址 | `--listen` |

## 当前可用 CLI 参数（仅 `serve`）

- `--config`
- `--db-path`
- `--listen`
- `--model`
- `--base-url`
- `--stream`
- `--timeout`
- `--allow-write`

## 推荐配置文件

- 运行配置：[`docs/configs/serve-web-ollama.json`](configs/serve-web-ollama.json)
- 模板配置：[`docs/configs/config-template.json`](configs/config-template.json)