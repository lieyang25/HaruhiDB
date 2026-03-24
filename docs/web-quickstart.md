# HaruhiDB Web 快速上手（Web-Only）

## 最短路径

1. 在树莓派上配置并启动服务
2. 在本机浏览器打开 `http://<pi-ip>:8080/ui`
3. 使用 Action JSON 控制台执行动作

## 树莓派命令

```bash
cd /home/pi/HaruhiDB
./scripts/start_web_one_click.sh --config docs/configs/serve-web-rpi.json
```

## 健康检查

```bash
curl -s http://127.0.0.1:8080/healthz
curl -s "http://127.0.0.1:8080/v1/capabilities"
```

## 接口清单

- `GET /ui`
- `GET /healthz`
- `GET /v1/capabilities`
- `POST /v1/action`

说明：`POST /v1/nl/translate` 已移除。

完整流程、14 动作表与示例见：

- [Web 一页上手与动作速查](web-action-one-page-guide.md)
