# 配置文件说明（Web-Only）

`docs/configs` 当前保留：

- `serve-web.json`：本机默认配置
- `serve-web-rpi.json`：树莓派局域网访问配置（`0.0.0.0:8080`）
- `config-template.json`：最小模板

## 结构

```json
{
  "common": {
    "db_path": "../haruhidb-web.db",
    "allow_write": true,
    "timeout": "30s"
  },
  "serve": {
    "listen": ":8080"
  }
}
```

## 启动示例

```bash
cd /home/suzumiya/__code__/code/HaruhiDB/GO
go run ./cmd/haruhidb serve --config ../docs/configs/serve-web.json
```

树莓派部署（推荐）：

```bash
cd /home/pi/HaruhiDB
HARU_CONFIG=docs/configs/serve-web-rpi.json HARU_OPEN_BROWSER=false ./scripts/start_web_one_click.sh
```

## 覆盖规则

- 配置文件生效后，CLI 同名参数会覆盖配置值。
- 一键脚本 `start_web_one_click.sh` 默认只传 `--config`，不会再写死模型相关参数。
