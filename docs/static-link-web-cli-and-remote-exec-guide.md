# 静态库与远程联动指南（Web + serve）

这份文档聚焦当前保留形态：

- Go 侧只保留 `serve` 子命令
- 通过 Web UI 或 HTTP API 远程使用
- 模型后端固定为 Ollama

## 1) 运行模式

1. 动态链接（默认）
- `go run ./cmd/haruhidb serve ...`
- 依赖 `CXX/build/src/capi` 动态库

2. 静态链接（可选）
- 若你本地有静态构建流程，可继续沿用 `haru_static` 标签方案
- 但对 Web/HTTP 功能本身没有额外差异

## 2) 推荐启动方式

### Linux

```bash
cd /path/to/HaruhiDB
./scripts/start_web_one_click.sh
```

### Windows

```powershell
cd E:\__code__\HaruhiDB
./scripts/start_web_one_click.ps1
```

脚本会自动完成：

1. 检查依赖
2. 探测并拉起 Ollama
3. 拉取模型
4. 自动构建 C API 库（缺失时）
5. 启动 Go `serve`

## 3) 手动启动（适合服务器）

```bash
cd /path/to/HaruhiDB/GO
go run ./cmd/haruhidb serve --config ../docs/configs/serve-web-ollama.json
```

常用覆盖参数：

- `--listen 0.0.0.0:8080`
- `--db-path /data/haruhidb/web.db`
- `--model qwen2.5-coder:3b`
- `--base-url http://127.0.0.1:11434`

## 4) 远程联动

服务端启动后：

- 网页：`http://<server-ip>:8080/ui`
- 健康：`http://<server-ip>:8080/healthz`
- API：`/v1/action`、`/v1/nl/translate`

建议部署顺序：

1. 先验证 `/healthz`
2. 再验证 `/v1/action`
3. 最后联调 `/v1/nl/translate`

## 5) 最小验收清单

1. `/healthz` 返回 `{"ok":true}`
2. `/ui` 可访问并完成“仅翻译”
3. “翻译并执行”可跑通预期动作链
4. 重启服务后数据库文件仍可继续访问