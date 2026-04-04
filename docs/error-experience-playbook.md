# 常见错误排查（Web-Only）

## 1. `unsupported action "xxx"`

原因：动作名不在当前 14 个支持动作里，或拼写错误。

处理：

1. 调 `GET /v1/capabilities` 查看 `actions`
2. 对照动作名后重试

## 2. `write actions are disabled by server configuration`

原因：服务端配置 `common.allow_write=false`。

处理：

1. 改配置为 `true`
2. 重启服务

## 3. `context deadline exceeded`

原因：请求超时（常见于 `batch` 太大、树莓派资源紧张）。

处理：

1. 增大 `common.timeout`（如 `30s -> 60s`）
2. 拆分大批量 `batch`
3. 重启并复测

## 4. `table "xxx" not found`

原因：当前连接的 `db_path` 不是你预期的数据库。

处理：

1. 在 UI 里确认数据库路径
2. 调 `list_tables` 验证当前库
3. 必要时切换 `db_path`
