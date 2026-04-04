# HaruhiDB Go 使用指南（Web-Only）

## 1. 入口

当前 Go CLI 只保留一个子命令：`serve`。

```bash
cd /home/suzumiya/__code__/code/HaruhiDB/GO
go run ./cmd/haruhidb help
```

## 2. 启动方式

### 2.1 使用配置文件（推荐）

```bash
go run ./cmd/haruhidb serve --config ../docs/configs/serve-web.json
```

### 2.2 直接传参数

```bash
go run ./cmd/haruhidb serve \
  --db-path /tmp/haruhidb-demo.db \
  --listen :8080 \
  --timeout 30s \
  --allow-write=true
```

## 3. `serve` 参数

- `--config`
- `--db-path`
- `--listen`
- `--timeout`
- `--allow-write`

## 4. HTTP 接口

- `GET /healthz`
- `GET /v1/capabilities`
- `POST /v1/action`
- `GET /ui`

## 5. 快速验证

```bash
curl -s http://127.0.0.1:8080/healthz

curl -s http://127.0.0.1:8080/v1/action \
  -H 'Content-Type: application/json' \
  -d '{"version":"v3","request_id":"req-1","mode":"read_only","action":"list_tables","args":{}}'
```
