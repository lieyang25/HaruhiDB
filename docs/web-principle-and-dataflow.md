# HaruhiDB Web 原理与信息流

这份文档回答 4 个问题：

1. 网页是怎么工作的
2. 页面里展示的 JSON 分别是什么
3. 为什么要这么设计
4. 后续可以如何改造

## 1) 架构原理（同进程、同端口）

当前 Web Console 不是单独的前端服务，而是 Go `serve` 进程内置静态资源：

- `GET /` 重定向到 `GET /ui`
- `GET /ui` 返回 `index.html`
- `GET /ui/*` 返回 `app.js/styles.css` 等静态文件
- `POST /v1/nl/translate` 做自然语言 -> Action 翻译
- `POST /v1/action` 执行 Action

这意味着：

- 部署简单（一个进程）
- UI 与 API 天然同源（无额外 CORS 配置）
- 端口统一（默认 `:8080`）

## 2) 信息流方向（从输入到落库）

网页按钮“仅翻译”与“翻译并执行”对应两段式流程。

### A. 仅翻译

1. 浏览器把 `request_id/input/mode` 发到 `/v1/nl/translate`
2. 服务端读取当前 catalog 快照（表、列、索引）
3. 服务端把自然语言 + catalog + 协议约束发给模型
4. 服务端对模型输出做：
   - JSON 提取与归一化（别名、多余字段修剪、`args` 兼容）
   - 严格协议校验（版本、动作、字段、表是否存在、类型约束）
5. 返回 `Translation Result`

### B. 翻译并执行

1. 先执行上面的“仅翻译”流程
2. 只有当 `valid=true` 且存在 `candidate_envelope` 时
3. 前端把 `candidate_envelope` 原样提交到 `/v1/action`
4. 服务端解码 + 校验 + 分发执行，返回 `Execution Result`

## 3) 页面输出的 JSON 含义

### `Translation Result`

来自 NL 翻译接口，核心字段：

- `request_id`：这次 NL 请求 ID
- `nl_input`：原始自然语言
- `candidate_envelope`：可执行的 Action 请求（仅 `valid=true` 时出现）
- `valid`：翻译结果是否通过协议与 catalog 校验
- `error`：翻译失败原因（例如表不存在、字段不合法）
- `meta`：模型名、耗时、provider、可选 `raw_output`

### `Execution Result`

来自 Action 执行接口，核心字段：

- `ok`：整体是否成功
- `action`：顶层动作（如 `batch`）
- `data`：动作结果（`batch` 下含每步 `results`）
- `error`：协议或执行错误
- `meta`：预留扩展信息

## 4) 为什么这样设计

当前设计目标是“安全可控、便于调试”：

- 先翻译后执行：避免模型直接落库，先给人看可执行计划
- 严格结构校验：防止小模型输出散文、伪 JSON 或越界字段
- catalog 先验校验：在执行前尽早发现 `table not found` 等错误
- `meta.raw_output` 可选透出：便于定位“模型问题还是解析问题”

## 5) 后续可改造点

可以改，而且改造成本较低：

1. UI 层改造
- 增加“动作白名单/黑名单”开关
- 增加“执行前人工勾选确认”
- 增加历史记录与回放

2. 协议层改造
- 扩展新动作（例如 DDL）
- 引入版本化（`v2`）避免影响 `v1`
- 为 `batch` 引入更细粒度执行策略

3. 模型层改造
- 更严格 system prompt
- 更强的 examples 注入策略
- 针对小模型增加 repair 轮次或规则修复器

## 6) 当前已知边界

- Action v1 只暴露 DML/查询与元数据查询，不含 DDL
- 因此“从空库到可用表”的全流程还不能只靠 v1 Action 完成
- 若要闭环，需要补充 DDL 动作（建议走 v2，见后续文档）