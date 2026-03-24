# Go 阅读指南（当前版本：Web + serve + Ollama）

这份指南只覆盖**当前 Go 侧真实形态**：

- 入口只保留 `serve`
- 对外能力是 Web UI + HTTP API
- 自然语言翻译固定走 Ollama
- 动作集统一为 v3（兼容接收 v1/v2 输入）

如果你想快速接手 Go 侧，这份文档按“最短主线”组织阅读路径。

## 1. 先建立整体图

```text
Browser/UI
  -> /v1/nl/translate
  -> /v1/action

cmd/haruhidb serve
  -> ActionService
    -> action.Decode/Validate/Execute
    -> nl.OllamaTranslator
    -> haruhidb.DB (CGO C API)
```

核心点：

1. `serve` 是唯一 CLI 入口。
2. `ActionService` 是业务中枢（执行 + NL 翻译）。
3. `action` 包是协议和动作分发中心。
4. `haruhidb` 包是 Go 到 C API 的桥。

## 2. 30 分钟最短阅读路径

按顺序读下面 7 个文件，能最快建立完整心智模型：

1. `GO/cmd/haruhidb/main.go`
2. `GO/cmd/haruhidb/config.go`
3. `GO/internal/transport/http/server.go`
4. `GO/internal/app/service.go`
5. `GO/internal/action/decode.go`
6. `GO/internal/action/execute.go`
7. `GO/internal/nl/ollama.go`

读法建议：

- 先看“入口怎么把对象连起来”（`main.go`）
- 再看“请求怎么进来和出去”（`server.go`）
- 最后看“动作执行 + NL 翻译核心逻辑”（`service/action/nl`）

## 3. 三条关键请求链路

### 3.1 `/v1/action` 执行链路

```text
HTTP /v1/action
  -> ActionService.ExecuteJSON
  -> action.Decode
  -> action.ValidateEnvelope
  -> action.ExecuteEnvelope
  -> haruhidb.DB.*
```

### 3.2 `/v1/nl/translate` 翻译链路

```text
HTTP /v1/nl/translate
  -> ActionService.TranslateNL
  -> loadCatalogSnapshot
  -> OllamaTranslator.Translate
  -> candidate normalization + validation
  -> NLResult(valid/candidate_envelope/error)
```

### 3.3 UI 两段式链路

```text
/ui
  1) 先调 /v1/nl/translate 预览
  2) valid=true 后再调 /v1/action 执行
```

## 4. 模块职责速查

### `GO/cmd/haruhidb`

- `main.go`：`serve` 启动、参数绑定、服务装配。
- `config.go`：`--config`/`HARUHIDB_CONFIG` 解析，配置映射到运行参数。

### `GO/internal/transport/http`

- `server.go`：路由、请求解码、错误映射。
- `ui/*`：内置网页静态资源。

### `GO/internal/app`

- `service.go`：Action 与 NL 的统一编排层。
- 职责包括：写保护、超时、catalog 快照、候选校验与归一化。

### `GO/internal/action`

- `types.go`：动作、版本、请求/响应结构。
- `decode.go`：严格解码 + 协议校验。
- `execute.go`：动作分发与执行。
- `validator.go`：参数/表结构/类型约束校验。

### `GO/internal/nl`

- `ollama.go`：Ollama 翻译器，支持流式与 JSON 提取。
- `types.go`：翻译器接口与 catalog 快照结构。

### `GO/haruhidb`

- `haruhidb.go`：CGO 封装，提供 DDL/DML/扫描/元数据 API。

## 5. 当前对外能力边界

1. CLI：只支持 `haruhidb serve`。
2. HTTP：`/healthz`、`/v1/action`、`/v1/nl/translate`、`/ui`。
3. NL 后端：仅 Ollama（`--base-url` + `--model` + `--stream`）。
4. 动作集：统一以 v3 执行与输出，兼容 v1/v2 旧请求。

## 6. 当前可用参数（serve）

- `--config`
- `--db-path`
- `--listen`
- `--model`
- `--base-url`
- `--stream`
- `--timeout`
- `--allow-write`

## 7. 常见改动从哪里下手

### 7.1 改路由/返回结构

先看：`GO/internal/transport/http/server.go`。

### 7.2 改动作语义或加新动作

先看：`GO/internal/action/types.go` -> `decode.go` -> `execute.go` -> `validator.go`。

### 7.3 调整 NL 翻译策略

先看：`GO/internal/nl/ollama.go` 与 `GO/internal/app/service.go` 里候选归一化逻辑。

### 7.4 改配置项

先看：`GO/cmd/haruhidb/config.go` + `GO/cmd/haruhidb/main.go`。

## 8. 推荐验证顺序

1. `go test ./... -run '^$'`（先做全包编译检查）
2. `go test ./internal/transport/http -run TestHealthz -count=1`
3. 手工启动 `serve` 后访问 `/healthz` 与 `/ui`
4. 用 UI 走一遍“仅翻译 -> 翻译并执行”

## 9. 一句话总结

当前 Go 侧可以理解为：

- 一个 `serve` 进程
- 暴露 Web + HTTP
- 内部通过 Action 协议和 Ollama 翻译器驱动 HaruhiDB C API

阅读时始终围绕这条主线，就不会迷路。
