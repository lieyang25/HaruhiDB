# HaruhiDB Web 原理与信息流（当前版本）

这份文档回答 4 个问题：

1. 网页是怎么工作的
2. 页面里展示的 JSON 含义是什么
3. 当前为什么采用两段式执行
4. 后续可改造点有哪些

## 1) 架构原理（同进程、同端口）

Web Console 不是独立前端服务，而是 Go `serve` 进程内置静态资源：

- `GET /` 重定向到 `GET /ui`
- `GET /ui` 返回 `index.html`
- `GET /ui/*` 返回 `app.js/styles.css` 等静态文件
- `POST /v1/nl/translate` 做自然语言 -> Action 翻译
- `POST /v1/action` 执行 Action

这意味着：

- 部署简单（一个进程）
- UI 与 API 天然同源（无需额外 CORS）
- 端口统一（默认 `:8080`）

## 2) 信息流方向（从输入到落库）

网页按钮“仅翻译”与“翻译并执行”是两段式流程。

### A. 仅翻译

1. 浏览器把 `request_id/input/mode` 发到 `/v1/nl/translate`
2. 服务端读取 catalog 快照（表、列、索引）
3. 服务端把自然语言 + catalog + 协议约束发给 Ollama
4. 服务端对模型输出做：
   - JSON 提取与归一化（别名、多余字段修剪、`args` 兼容）
   - 严格协议校验（版本、动作、字段、表是否存在、类型约束）
5. 返回 `Translation Result`

### B. 翻译并执行

1. 先执行“仅翻译”
2. 只有当 `valid=true` 且存在 `candidate_envelope` 时
3. 前端把 `candidate_envelope` 原样提交到 `/v1/action`
4. 服务端解码 + 校验 + 分发执行，返回 `Execution Result`

## 3) 页面输出 JSON 含义

### `Translation Result`

来自 NL 翻译接口，核心字段：

- `request_id`：本次 NL 请求 ID
- `nl_input`：原始自然语言
- `candidate_envelope`：可执行 Action 请求（仅 `valid=true` 出现）
- `valid`：翻译结果是否通过协议与 catalog 校验
- `error`：翻译失败原因（例如表不存在、字段不合法）
- `meta`：模型、耗时、provider 等附加信息

### `Execution Result`

来自 Action 执行接口，核心字段：

- `ok`：整体是否成功
- `action`：顶层动作（例如 `batch`）
- `data`：动作结果（`batch` 下含每步 `results`）
- `error`：协议或执行错误
- `meta`：预留扩展字段

## 4) 为什么采用两段式

当前目标是“可控 + 可观察”：

- 先翻译后执行：避免模型输出直接落库，先展示计划
- 严格结构校验：防止散文、伪 JSON、越界字段
- catalog 先验校验：在执行前尽早发现 `table not found` 等错误

## 5) 后续可改造点

1. UI 层
- 增加动作白名单/黑名单开关
- 增加执行前人工确认
- 增加历史记录与回放

2. 协议层
- 继续扩展版本化动作
- 为 `batch` 增加更细粒度执行策略

3. 模型层
- 调整 system prompt
- 优化示例注入策略
- 增加更强的 repair 策略

## 6) 当前能力边界

- CLI 只保留 `serve`
- NL 后端固定为 Ollama
- 动作协议支持 v1 与 v2
  - v1 侧重读写与查询动作
  - v2 在 v1 基础上补充 DDL（`create/drop table/index`）
