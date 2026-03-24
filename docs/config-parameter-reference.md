# HaruhiDB 配置参数总表（Web-Only）

## 配置优先级

1. CLI 参数（最高）
2. 配置文件（`--config` 或 `HARUHIDB_CONFIG`）
3. 程序默认值

## 配置结构

当前只保留三个顶层分组：`common`、`serve`、`launcher`。

### `common`

| 配置键 | 类型 | 说明 | 对应 CLI |
| --- | --- | --- | --- |
| `common.db_path` | string | 数据库文件路径 | `--db-path` |
| `common.allow_write` | bool | 是否允许写动作 | `--allow-write` |
| `common.timeout` | string(duration) | 单请求超时 | `--timeout` |

### `serve`

| 配置键 | 类型 | 说明 | 对应 CLI |
| --- | --- | --- | --- |
| `serve.listen` | string | 监听地址 | `--listen` |

### `launcher`（启动脚本层）

| 配置键 | 类型 | 说明 | 对应脚本参数 |
| --- | --- | --- | --- |
| `launcher.open_browser` | bool | 启动后是否自动打开浏览器 | `--open-browser` |
| `launcher.ui_url` | string | 自动打开的 UI 地址（可选） | `--ui-url` |

## `serve` 当前可用 CLI 参数

- `--config`
- `--db-path`
- `--listen`
- `--timeout`
- `--allow-write`

## 已移除项

- 配置分组：`ollama.*`
- CLI 参数：`--model`、`--base-url`、`--stream`
- 接口：`POST /v1/nl/translate`
- 启动语义：`HARU_MODEL`、`HARU_BASE_URL`、`HARU_STREAM`

## 推荐配置文件

- 本机默认：`docs/configs/serve-web.json`
- 树莓派局域网：`docs/configs/serve-web-rpi.json`
