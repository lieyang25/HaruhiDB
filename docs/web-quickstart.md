# HaruhiDB 网页快速上手

这份文档面向「想直接用网页」的场景。

HaruhiDB 的网页 UI 由 Go `serve` 进程内置提供，和 API 同端口同进程。

- 网页入口：`GET /ui`
- API 健康：`GET /healthz`
- Action 执行：`POST /v1/action`
- NL 翻译：`POST /v1/nl/translate`
- 报错排查：[错误经验汇总](error-experience-playbook.md)
- 设计原理：[Web 原理与信息流](web-principle-and-dataflow.md)

## 一键启动（推荐）

### Windows (PowerShell)

```powershell
cd E:\__code__\HaruhiDB
./scripts/start_web_one_click.ps1
```

### Linux

```bash
cd /path/to/HaruhiDB
./scripts/start_web_one_click.sh
```

脚本会自动：

1. 检查依赖（`go`、`ollama`、`cmake`，Windows 额外检查 `mingw32-make`）
2. 确保 Ollama 服务可用
3. 拉取模型（默认 `qwen2.5-coder:3b`）
4. 确保 C API 动态库存在（缺失时自动构建）
5. 启动 Go 服务并打开 `http://127.0.0.1:8080/ui`

## 环境变量

- `HARU_MODEL`：默认 `qwen2.5-coder:3b`
- `HARU_DB_PATH`：数据库路径
- `HARU_LISTEN`：默认 `:8080`
- `HARU_TIMEOUT`：默认 `60s`
- `HARU_OPEN_BROWSER`：是否自动打开浏览器（默认 `true`）
- `HARU_UI_URL`：覆盖自动打开的网址

示例：

```powershell
$env:HARU_MODEL='qwen2.5-coder:3b'
$env:HARU_LISTEN=':9090'
$env:HARU_OPEN_BROWSER='false'
./scripts/start_web_one_click.ps1
```

## 运行逻辑（网页端）

网页里的「翻译并执行」是两段式：

1. 先调用 `/v1/nl/translate`
2. 若返回 `valid=true`，再将 `candidate_envelope` 提交到 `/v1/action`

默认建议先用「仅翻译」预览，再执行。

## 常见问题

1. 打开网页失败
- 先访问 `http://127.0.0.1:8080/healthz`，确认服务已起。

2. 翻译结果动作跑偏
- 在输入框使用“严格四步模板”语气。
- 优先使用 `qwen2.5-coder:3b`，并保留 `examples_path=../docs/action-v1-model-spec.json`。

3. Windows 启动时报找不到 `libharuhidb_capi.dll.a`
- 直接用 `scripts/start_web_one_click.ps1`，脚本会自动构建并复制到 `CXX/build/src/capi`。


更多排查案例见：[错误经验汇总](error-experience-playbook.md)。
