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

### Linux / macOS

```bash
cd /path/to/HaruhiDB
./scripts/start_web_one_click.sh
```

脚本会自动：

1. 检查依赖（`go`；若 `HARU_BASE_URL` 指向本机 Ollama，则还需要 `ollama`；缺少 C API 库时额外需要 `cmake`）
2. 确保 Ollama 服务可用
3. 拉取模型（仅本机 Ollama；默认 `qwen2.5-coder:3b`）
4. 确保 C API 动态库存在（缺失时自动构建）
5. 启动 Go 服务并打开 `http://127.0.0.1:8080/ui`

## 环境变量

- `HARU_MODEL`：默认 `qwen2.5-coder:3b`
- `HARU_BASE_URL`：默认 `http://127.0.0.1:11434`
- `HARU_DB_PATH`：数据库路径
- `HARU_LISTEN`：默认 `:8080`
- `HARU_TIMEOUT`：默认 `120s`
- `HARU_STREAM`：默认 `false`（推荐，稳定性更高）
- `HARU_ALLOW_WRITE`：默认 `true`
- `HARU_OPEN_BROWSER`：是否自动打开浏览器（默认 `true`）
- `HARU_UI_URL`：覆盖自动打开的网址
- `HARU_CAPI_DIR`：C API 链接目录
- `HARU_CAPI_RUNTIME_DIR`：C API 运行时目录（`.so`/`.dylib`）
- `HARU_CGO_CFLAGS`：覆盖 `CGO_CFLAGS`
- `HARU_CGO_LDFLAGS`：覆盖 `CGO_LDFLAGS`

示例：

```bash
export HARU_MODEL='qwen2.5-coder:3b'
export HARU_LISTEN=':9090'
export HARU_OPEN_BROWSER='false'
./scripts/start_web_one_click.sh
```

树莓派远程 Ollama 示例：

```bash
export HARU_BASE_URL='http://192.168.137.236:11434'
export HARU_MODEL='qwen2.5-coder:0.5b'
export HARU_OPEN_BROWSER='false'
./scripts/start_web_one_click.sh
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
- 优先使用 `qwen2.5-coder:3b`。

3. 找不到数据库文件
- 一键脚本默认落盘：`<repo>/haruhidb-web.db`。
- 手动 `--config` 默认落盘：`<repo>/haruhidb-web.db`（配置模板已改为相对路径）。
- 每次执行一键脚本后，可直接查看 `<repo>/.haruhidb_db_path` 获取当前绝对路径。

更多排查案例见：[错误经验汇总](error-experience-playbook.md)。
