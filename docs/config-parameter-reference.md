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

## 脚本层可选覆盖（`start_web_one_click.sh`）

- `HARU_DB_PATH`：覆盖数据库路径（映射 `--db-path`）
- `HARU_LISTEN`：覆盖监听地址（映射 `--listen`）
- `HARU_TIMEOUT`：覆盖超时（映射 `--timeout`）
- `HARU_ALLOW_WRITE`：覆盖写开关（映射 `--allow-write`）

## 推荐配置文件

- 本机默认：`docs/configs/serve-web.json`
- 树莓派局域网：`docs/configs/serve-web-rpi.json`
