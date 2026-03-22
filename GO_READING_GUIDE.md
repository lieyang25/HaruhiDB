# HaruhiDB Go 侧阅读指南

这份文档不是 Go API 名录，也不是功能宣传页。

它只做一件事：

- 帮你从仓库最顶层出发，用最省力的路径读懂 HaruhiDB 的 Go 侧

如果你是第一次看这个项目，最容易犯的错是：

- 一上来就钻进 `GO/haruhidb/haruhidb.go` 里的 cgo 细节
- 一上来就看 `CXX/`，结果 Go 侧还没建立地图
- 一上来就看 `nl`，把“可选能力”当成“主干能力”

这份阅读指南的原则正好相反：

- 先看最小闭环
- 先看调用链，再看实现细节
- 先看 Action 主线，再看 NL 支线
- 先把 Go 侧看成“上层适配与协议层”，最后再回头对接 C++ 内核

---

## 1. 先建立最重要的认知

HaruhiDB 当前的 Go 侧，本质上不是数据库内核。

它更准确的定位是：

- 一个建立在 C++ 数据库内核之上的 Go 接入层
- 一个把“数据库能力”包装成 CLI / HTTP / Action Protocol / NL 翻译能力的上层

如果只记一句话：

- `CXX/` 负责“数据库怎么真正工作”
- `GO/` 负责“这些能力怎样被 Go 程序、CLI、HTTP、模型翻译层使用”

所以在 Go 侧阅读时，你要先问的不是“页怎么刷盘”，而是：

- 这个请求从哪里进来？
- 在 Go 里先被谁接住？
- 谁做参数校验？
- 谁决定是否允许写？
- 谁真正把动作压到数据库？
- 什么时候才下沉到 C++？

---

## 2. 最简地图

先把整个 Go 侧压成一张图：

```text
User / Test / Shell / HTTP Client
        |
        v
GO/cmd/haruhidb
        |
        v
GO/internal/app
        |
        +--> GO/internal/action  ------> GO/haruhidb ------> CXX/
        |
        +--> GO/internal/nl ------> model backend
        |
        `--> GO/internal/transport/http
```

这张图里最重要的事实有 4 个：

1. `cmd/haruhidb` 是入口层
2. `internal/app` 是编排层
3. `internal/action` 是主干协议执行层
4. `GO/haruhidb` 是 Go 和 C++ 的边界层

其中：

- `internal/action` 是主干
- `internal/nl` 是可选支线
- `internal/transport/http` 是薄封装
- `GO/haruhidb` 是边界，不是第一阅读入口

---

## 3. 目录总表：每一层看什么

| 目录 / 文件 | 角色 | 什么时候看 | 重点看什么 | 看完后你应该能回答什么 |
| --- | --- | --- | --- | --- |
| `README.md` | 项目总地图 | 第 1 步 | 整体架构、C++ 主线、Go 文档入口 | Go 侧在整个项目里处于哪一层？ |
| `docs/go-usage-guide.md` | Go 使用与链路说明 | 第 1 步 | Action 链与 NL 链的区别 | 为什么 `run` 和 `nl` 不是一回事？ |
| `docs/action-protocol-v1.md` | 协议定义 | 第 1 步 | 请求信封、动作枚举、边界条件 | Go 侧到底在执行什么协议？ |
| `GO/cmd/haruhidb/main.go` | CLI 入口 | 第 2 步 | 子命令分发、参数绑定、`buildService()` | 一个命令是怎么进入系统的？ |
| `GO/cmd/haruhidb/config.go` | 配置加载 | 第 2 步 | `--config`、环境变量、默认值优先级 | CLI / 配置文件 / 默认值谁覆盖谁？ |
| `GO/internal/app/service.go` | 应用编排层 | 第 3 步 | 超时、写保护、NL 翻译、重试修复 | Go 侧在哪一层做策略控制？ |
| `GO/internal/action/types.go` | 协议类型定义 | 第 4 步 | `Mode`、`Action`、请求响应结构 | 支持哪些动作，响应长什么样？ |
| `GO/internal/action/decode.go` | 协议解码与校验入口 | 第 4 步 | `Decode()`、`ValidateEnvelope()` | JSON 如何变成内部请求对象？ |
| `GO/internal/action/validator.go` | 参数与语义校验 | 第 4 步 | 表存在性、索引要求、值类型转换 | 哪些错误在真正执行前就被挡住？ |
| `GO/internal/action/execute.go` | 协议执行层 | 第 4 步 | 每个动作如何映射到 DB API | `list_tables` / `scan_all` / `insert_row` 到底怎么执行？ |
| `GO/internal/transport/http/server.go` | HTTP 适配层 | 第 5 步 | 路由、限流、鉴权、请求体读取 | HTTP 层是业务层还是薄封装？ |
| `GO/internal/nl/openai.go` | NL 翻译器 | 第 6 步 | Prompt、请求模型、抽 JSON | 自然语言怎样被翻译成 Action JSON？ |
| `GO/haruhidb/haruhidb.go` | Go 到 C++ 的 cgo 边界 | 第 7 步 | 类型转换、错误模型、Scanner | Go 侧最后是怎样落到 C API 的？ |
| `GO/haruhidb/haruhidb_test.go` | Go DB API 示例 | 第 2 步起配合读 | 最小 round-trip | `DB` 这一层最基本的用法是什么？ |
| `GO/cmd/haruhidb/main_test.go` | CLI 用例 | 第 2 步起配合读 | `run` / `nl` / `shell` 的 happy path | 用户从 CLI 看到的行为是什么？ |
| `GO/internal/action/action_test.go` | 协议文档与校验示例 | 第 4 步配合读 | 文档示例如何被校验 | 文档和代码是否对齐？ |
| `GO/internal/action/action_execute_test.go` | 执行动作的行为样例 | 第 4 步配合读 | 所有主要动作的行为 | 每个 Action 的成功与失败长什么样？ |
| `GO/internal/app/service_test.go` | 编排层样例 | 第 3 步配合读 | 写保护、NL 修复重试 | service 做了哪些额外策略？ |
| `GO/internal/transport/http/server_test.go` | HTTP 行为样例 | 第 5 步配合读 | `/healthz`、`/v1/action`、`/v1/nl/translate` | 网络行为与本地调用如何对应？ |
| `CXX/docs/` | C++ 内核地图 | 第 8 步 | Runtime / Execution / Storage 文档 | Go 压下去后，C++ 接的是什么？ |

---

## 4. 最推荐的阅读顺序

如果你的目标是“从基层开始”，下面是我最推荐的顺序。

### 第 0 站：先把边界讲明白

先读：

- [`README.md`](README.md)
- [`docs/go-usage-guide.md`](docs/go-usage-guide.md)
- [`docs/action-protocol-v1.md`](docs/action-protocol-v1.md)

这一步不要急着看实现。

你只需要确认 3 件事：

1. Go 侧不是数据库内核
2. Go 侧有两条链路
   - Action 执行链
   - NL 翻译链
3. 当前系统真正对外承诺的是 Action Protocol v1

如果这一步没建立好，后面很容易把“CLI 表现”“协议校验”“内核能力”混成一团。

---

### 第 1 站：先看测试，不要先看实现

先读：

- [`GO/cmd/haruhidb/main_test.go`](GO/cmd/haruhidb/main_test.go)
- [`GO/haruhidb/haruhidb_test.go`](GO/haruhidb/haruhidb_test.go)

为什么先看这里：

- 测试给的是“最小可运行故事”
- 它们通常比实现更接近“这个模块是给谁用的”
- 你先知道输入和输出，再去追中间层，理解成本会低很多

这一步重点不是背代码，而是建立用户视角：

- `run` 命令怎么用
- `nl` 命令长什么样
- `shell` 模式怎么走
- `DB` 有哪些核心方法
- `ScanAll()` / `Scanner.Next()` 怎样配合

看完之后，你应该能说清：

- “如果我不用 HTTP，只用本地 CLI 或 Go API，最小闭环是什么？”

---

### 第 2 站：只追一条最小主线，不要扩散

建议你只追这条线：

```text
haruhidb run --json ... list_tables
```

从这里开始看：

- [`GO/cmd/haruhidb/main.go`](GO/cmd/haruhidb/main.go)

最先关注这些函数：

- `run()`
- `runRun()`
- `buildService()`
- `executeAndPrint()`

这一步只要抓住下面这条调用链就够了：

```text
run()
  -> runRun()
  -> buildService()
  -> service.ExecuteJSON()
  -> action.Decode()
  -> action.ExecuteEnvelope()
  -> action.ValidateEnvelope()
  -> action.Execute()
  -> executeListTables()
  -> db.ListTables()
```

这一步的目标不是理解所有动作，而是理解：

- 一个 CLI 请求是怎样被一路送进执行层的

如果你觉得代码多，先只盯住 `runRun()`，不要同时看 `runServe()`、`runNL()`、`runShell()`。

---

### 第 3 站：理解 service 层到底在补什么

接着读：

- [`GO/internal/app/service.go`](GO/internal/app/service.go)
- [`GO/internal/app/service_test.go`](GO/internal/app/service_test.go)

这是 Go 侧非常关键的一层。

因为它不直接定义协议，也不直接调用 C++ 细节。
它做的是“编排与策略”。

这里最值得抓住的点有：

1. `ExecuteJSON()`
   - 加请求超时
   - 先解协议
   - 做服务级写保护
   - 再把请求交给 `action.ExecuteEnvelope()`

2. `TranslateNL()`
   - 检查输入是否为空
   - 检查 mode 是否合法
   - 检查 translator 是否配置
   - 读取 catalog snapshot
   - 调模型生成候选 JSON
   - 如果候选 JSON 不合法，带着错误信息做一次 repair retry

3. `validateCandidateEnvelope()`
   - NL 输出不是直接信任
   - 它会再走一次 Action 协议解码和校验

这里是很多人第一次看时会忽略的关键：

- NL 不是直接“生成就执行”
- 它生成的是候选协议 JSON
- 候选 JSON 还要经过和正常 Action 请求一样的验证链

看完这一步，你应该能说清：

- 为什么 service 层不是多余的一层
- 为什么写保护不只存在于动作校验里
- 为什么 NL 翻译失败并不一定是模型错，也可能是协议校验挡住了

---

### 第 4 站：把 Action 层啃透

这是 Go 侧最重要的一站。

建议按这个顺序读：

1. [`GO/internal/action/types.go`](GO/internal/action/types.go)
2. [`GO/internal/action/decode.go`](GO/internal/action/decode.go)
3. [`GO/internal/action/validator.go`](GO/internal/action/validator.go)
4. [`GO/internal/action/execute.go`](GO/internal/action/execute.go)
5. [`GO/internal/action/action_test.go`](GO/internal/action/action_test.go)
6. [`GO/internal/action/action_execute_test.go`](GO/internal/action/action_execute_test.go)

#### 4.1 先看 `types.go`

你要先知道协议世界里有哪些基本对象：

- `Mode`
- `Action`
- `RequestEnvelope`
- `Request`
- 各种 `Args`
- 各种响应 `Data`

这里的关键不是结构体语法，而是要建立这两个认知：

- 外部世界看到的是 `RequestEnvelope`
- 内部真正执行时使用的是 `Request`

也就是说：

- “外部 JSON”
- “内部带类型和预处理信息的请求对象”

不是同一个层次。

#### 4.2 再看 `decode.go`

这是协议入口。

这里最重要的是理解两件事：

1. `Decode()` 负责“把 JSON 读出来”
2. `ValidateEnvelope()` 负责“把 JSON 变成一个可执行的内部请求”

`ValidateEnvelope()` 做的工作很多：

- 检查版本
- 检查 `request_id`
- 检查 `mode`
- 检查 `action`
- 检查 `read_only` 下是否误用了写动作
- 检查 `args` 是否为 JSON object
- 按不同 action 解码不同 args
- 拉取表结构与索引元数据

这一步要特别建立一个感觉：

- Action 层不是“收到 JSON 就执行”
- 它先把 JSON 变成“已经被约束和补充信息的 Request”

#### 4.3 然后看 `validator.go`

这里是语义约束最密集的地方。

你会看到很多非常重要的边界都在这里：

- 表名是否合法
- 表是否存在
- 是否要求 primary-int index
- 首列是不是 `INTEGER NOT NULL`
- `limit` 的默认值与合法性
- JSON 数值如何变成 `haruhidb.Value`
- 哪些列可以写，哪些值类型不支持

这层非常值得花时间，因为它解释了：

- 为什么某些请求在执行前就失败
- 为什么协议边界不是“数据库报错以后再包装”

#### 4.4 最后看 `execute.go`

这里的结构反而是最直白的。

核心就是：

- `ExecuteEnvelope()`
- `Execute()`
- `executeAction()`
- 每个 `executeXxx()`

这里建议你优先看这几个动作：

1. `executeListTables()`
2. `executeDescribeTable()`
3. `executeScanAll()`
4. `executeGetByPrimaryInt()`
5. `executeInsertRow()`
6. `executeUpdateByPrimaryInt()`

它们覆盖了：

- 最简单的 metadata read
- schema 依赖
- scanner 流式读取
- index 定位
- 写入与值排序
- update 的“先读当前行再合并 patch”

看完 Action 层之后，你应该能说清：

- 一个 Action 请求从 JSON 到执行结果的全过程
- 哪些失败属于协议失败
- 哪些失败属于底层 DB 失败
- 为什么 `internal/action` 是 Go 侧真正的主干

---

### 第 5 站：HTTP 层最后再看

读：

- [`GO/internal/transport/http/server.go`](GO/internal/transport/http/server.go)
- [`GO/internal/transport/http/server_test.go`](GO/internal/transport/http/server_test.go)

HTTP 层建议放后面看，因为它其实比较“薄”。

它做的事情主要是：

- 路由分发
- 方法检查
- 鉴权
- 限流
- 读取请求体
- 调 service
- 把结果写回 HTTP 响应

如果你先去看 HTTP，容易误以为这是系统核心。
其实它只是把同一套 service/action 能力挂到网络上。

这里看完你要建立的认知是：

- HTTP 层不应该承载核心业务规则
- 核心规则应尽量在 `service` 和 `action` 内部

---

### 第 6 站：NL 是支线，不是起点

读：

- [`GO/internal/nl/types.go`](GO/internal/nl/types.go)
- [`GO/internal/nl/openai.go`](GO/internal/nl/openai.go)

这里最重要的是不要把它读重了。

NL 这一层当前的职责非常聚焦：

- 准备 prompt
- 带上 catalog snapshot
- 调模型接口
- 从模型响应里抽出 JSON 对象

它不负责：

- 真正执行协议
- 真正校验数据库语义
- 直接修改底层数据库行为

你应该把它看成：

- “协议生成器”

而不是：

- “数据库执行器”

这里有两个关键细节值得注意：

1. prompt 里带了 catalog snapshot
   - 说明它不是裸翻译
   - 它在尽量利用当前数据库结构信息

2. 响应只要不是合规 JSON object，就会失败
   - 说明系统对模型输出并不是放任使用

看完这里，你应该能说清：

- NL 功能只是把自然语言转成 Action 请求
- 真正的约束仍然由 Action 层把关

---

### 第 7 站：最后再看 cgo 边界

读：

- [`GO/haruhidb/doc.go`](GO/haruhidb/doc.go)
- [`GO/haruhidb/haruhidb.go`](GO/haruhidb/haruhidb.go)
- [`GO/haruhidb/haruhidb_test.go`](GO/haruhidb/haruhidb_test.go)

这是很多人最想先看的地方，但从阅读效率上讲，它应该放后面。

原因很简单：

- 这里解决的是“Go 如何调用底层 C API”
- 不是“系统顶层如何组织请求”

你在这一步重点要看的是：

1. 类型映射
   - `Type`
   - `Value`
   - `ColumnDef`
   - `ColumnInfo`

2. 错误模型
   - `Error`
   - `ErrorCode`
   - `errors.Is/errors.As` 支持

3. 资源管理
   - `Open()`
   - `Close()`
   - `Scanner.Close()`
   - `Scanner.Next()`

4. Go 到 C 的转换
   - `C.CString`
   - `unsafe.Pointer`
   - slice 到 C struct 数组的映射

看这一层时，心里一定要有一个判断标准：

- 这里主要是在做“边界转换”和“错误包装”
- 如果你在这里找“协议决策”“是否允许写”“动作语义”，方向就偏了

---

### 第 8 站：最后回到 C++ 文档

当 Go 侧主线已经清楚以后，再回到：

- [`CXX/docs/README.md`](CXX/docs/README.md)
- [`CXX/docs/architecture_overview.md`](CXX/docs/architecture_overview.md)
- [`CXX/docs/execution_and_dataflow.md`](CXX/docs/execution_and_dataflow.md)
- [`CXX/docs/persistence_and_recovery.md`](CXX/docs/persistence_and_recovery.md)

这时你看的就不是“陌生的大系统”了。

你会带着明确问题回去看：

- `db.ListTables()` 在 C++ 里最终依赖哪个对象？
- `ScanAll()` 最后是走 `TableHeap` 还是别的执行器路径？
- 索引扫描和顺序扫描在 C++ 里怎样区分？
- WAL、BufferPool、Catalog 在 C++ 里怎么组织？

这时候再看 C++，效率会高很多。

---

## 5. 一条最简单的阅读主线

如果你现在时间很少，只想先读懂一条最简单的链，我建议你只看这个：

```text
CLI run -> service.ExecuteJSON -> action.ValidateEnvelope -> action.Execute -> db.ListTables
```

对应文件：

1. [`GO/cmd/haruhidb/main.go`](GO/cmd/haruhidb/main.go)
2. [`GO/internal/app/service.go`](GO/internal/app/service.go)
3. [`GO/internal/action/decode.go`](GO/internal/action/decode.go)
4. [`GO/internal/action/execute.go`](GO/internal/action/execute.go)
5. [`GO/haruhidb/haruhidb.go`](GO/haruhidb/haruhidb.go)

对应问题：

1. 命令入口在哪里？
2. service 为什么存在？
3. 协议怎么校验？
4. 动作怎么落到 DB API？
5. DB API 怎么下沉到底层？

如果这条链你已经能独立复述，Go 侧就已经入门了。

---

## 6. 最简单的阅读方法：每次只解决一个问题

阅读这类仓库时，最省力的方法不是“从上到下读完所有文件”，而是每次带一个问题。

下面这张表可以直接拿来用。

| 你现在的问题 | 先看哪里 | 再看哪里 | 最后看哪里 |
| --- | --- | --- | --- |
| 命令是怎么进来的？ | `GO/cmd/haruhidb/main.go` | `GO/cmd/haruhidb/config.go` | `GO/cmd/haruhidb/main_test.go` |
| Action JSON 是怎么被解析的？ | `GO/internal/action/types.go` | `GO/internal/action/decode.go` | `GO/internal/action/action_test.go` |
| 为什么某个请求被拒绝？ | `GO/internal/action/validator.go` | `GO/internal/app/service.go` | `docs/action-protocol-v1.md` |
| 某个 Action 最后调用了哪个 DB 方法？ | `GO/internal/action/execute.go` | `GO/haruhidb/haruhidb.go` | `GO/internal/action/action_execute_test.go` |
| HTTP 做了哪些事？ | `GO/internal/transport/http/server.go` | `GO/internal/app/service.go` | `GO/internal/transport/http/server_test.go` |
| 自然语言为什么会翻译失败？ | `GO/internal/app/service.go` | `GO/internal/nl/openai.go` | `docs/go-usage-guide.md` |
| Go 为什么能调 C++？ | `GO/haruhidb/doc.go` | `GO/haruhidb/haruhidb.go` | `CXX/src/include/capi/haruhidb.h` |
| 真正的数据库能力在什么地方？ | `README.md` | `CXX/docs/architecture_overview.md` | `CXX/docs/execution_and_dataflow.md` |

---

## 7. 读代码时建议优先追的 3 条链

不要试图第一次就把所有链都读完。

先追下面 3 条就够了。

### 链路 A：Action 执行链

这是最重要的一条。

```text
run/shell(:json)/HTTP /v1/action
  -> service.ExecuteJSON()
  -> action.Decode()
  -> action.ValidateEnvelope()
  -> action.Execute()
  -> db method
```

你应该优先读懂这条。

### 链路 B：NL 翻译链

这是支线。

```text
nl/shell(:nl)/HTTP /v1/nl/translate
  -> service.TranslateNL()
  -> translator.Translate()
  -> candidate JSON
  -> validateCandidateEnvelope()
  -> 可选执行
```

这条链解决的是“怎么生成协议”，不是“怎么执行协议”。

### 链路 C：DB API 到 C++ 边界

这是边界链。

```text
action execute
  -> haruhidb.DB method
  -> C API
  -> C++ runtime / catalog / execution / storage
```

这条链建议最后再看。

---

## 8. 读每个文件时，建议重点抓哪些函数

| 文件 | 建议优先看的函数 / 结构 | 原因 |
| --- | --- | --- |
| `GO/cmd/haruhidb/main.go` | `run` / `runRun` / `runNL` / `buildService` | 它们定义了入口和主链路 |
| `GO/cmd/haruhidb/config.go` | `resolveConfig` / `loadRuntimeConfig` / `applyCommonConfig` | 理解配置优先级与来源 |
| `GO/internal/app/service.go` | `ExecuteJSON` / `TranslateNL` / `validateCandidateEnvelope` | 理解编排、保护、重试 |
| `GO/internal/action/types.go` | `Mode` / `Action` / `RequestEnvelope` / `Request` | 理解协议对象 |
| `GO/internal/action/decode.go` | `Decode` / `ValidateEnvelope` | 理解 JSON 到内部请求的入口 |
| `GO/internal/action/validator.go` | `requireKnownTable` / `validateInsertValues` / `normalizeValueForColumn` | 理解语义边界和类型转换 |
| `GO/internal/action/execute.go` | `ExecuteEnvelope` / `Execute` / `executeAction` / `executeScanAll` / `executeInsertRow` | 理解动作如何落地 |
| `GO/internal/transport/http/server.go` | `NewHandler` / `authorizeRequest` / `readLimitedBody` | 理解 HTTP 层实际多薄 |
| `GO/internal/nl/openai.go` | `NewOpenAITranslator` / `Translate` / `buildOpenAIUserPrompt` | 理解模型适配 |
| `GO/haruhidb/haruhidb.go` | `Open` / `CreateTable` / `InsertRow` / `ScanAll` / `Scanner.Next` | 理解 Go 与 C API 的连接点 |

---

## 9. 让阅读更简单的具体技巧

下面这些技巧很实用，而且和这个仓库很匹配。

### 技巧 1：先看 happy path，再看错误分支

比如先看：

- `list_tables`
- `scan_all`

再看：

- 写保护失败
- 参数非法
- 模型输出非法

因为 happy path 最容易让你建立主骨架。

### 技巧 2：同一时间只追一个动作

推荐顺序：

1. `list_tables`
2. `describe_table`
3. `scan_all`
4. `get_by_primary_int`
5. `insert_row`
6. `update_by_primary_int`
7. `batch`

不要一开始就同时追所有动作。

### 技巧 3：配合测试读，而不是只读实现

建议搭配关系如下：

| 实现文件 | 配套测试 |
| --- | --- |
| `GO/cmd/haruhidb/main.go` | `GO/cmd/haruhidb/main_test.go` |
| `GO/internal/app/service.go` | `GO/internal/app/service_test.go` |
| `GO/internal/action/decode.go` / `validator.go` | `GO/internal/action/action_test.go` |
| `GO/internal/action/execute.go` | `GO/internal/action/action_execute_test.go` |
| `GO/internal/transport/http/server.go` | `GO/internal/transport/http/server_test.go` |
| `GO/haruhidb/haruhidb.go` | `GO/haruhidb/haruhidb_test.go` |

### 技巧 4：先把一个“名词”固定住，再读它的实现

比如先固定这些概念：

- `RequestEnvelope`
- `Request`
- `ActionService`
- `Translator`
- `DB`
- `Scanner`

你先知道这些对象各自代表什么，再去看方法，理解会快很多。

### 技巧 5：把 NL 当成“协议生成器”

这会帮你减少很多误读。

你一旦把 NL 看成“执行器”，就会不自觉地到它里面寻找太多业务逻辑。
实际上，大多数业务约束还是在 `service` 和 `action`。

---

## 10. 常见误区

### 误区 1：Go 侧就是数据库实现

不是。

Go 侧更多是：

- 接入层
- 协议层
- 编排层
- 模型适配层

真正的存储、执行、索引、WAL 主线在 `CXX/`。

### 误区 2：HTTP 是主入口，所以应该先看 HTTP

不建议。

HTTP 在这里更像“对外暴露方式”。
真正主干逻辑仍然在 `service + action`。

### 误区 3：先看 cgo 最底层更扎实

不一定。

如果你先看 cgo，你只会先看到：

- `C.CString`
- `unsafe.Pointer`
- 各种桥接 struct

但你还不知道这些桥接是为哪条业务链服务的。

### 误区 4：NL 比 Action 更高级，所以应该先看 NL

也不建议。

NL 依赖 Action。

你如果没理解 Action Protocol，NL 层里的 prompt、catalog snapshot、candidate JSON 都会显得抽象。

### 误区 5：文档看完就等于懂实现

也不是。

正确顺序是：

- 文档建立地图
- 测试建立直觉
- 主链建立流程
- 实现建立细节

---

## 11. 如果你只有 30 分钟、半天、1 天，分别该怎么看

| 你有多少时间 | 推荐阅读内容 | 目标 |
| --- | --- | --- |
| 30 分钟 | `README.md` + `docs/go-usage-guide.md` + `GO/cmd/haruhidb/main_test.go` | 先知道系统边界和 Go 侧入口 |
| 半天 | 再加 `GO/cmd/haruhidb/main.go`、`GO/internal/app/service.go`、`GO/internal/action/decode.go`、`GO/internal/action/execute.go` | 读懂最小 Action 主线 |
| 1 天 | 再加 `GO/internal/action/validator.go`、`GO/internal/action/action_execute_test.go`、`GO/haruhidb/haruhidb.go` | 读懂协议校验、执行和 Go/C++ 边界 |
| 2 到 3 天 | 再加 `GO/internal/transport/http/server.go`、`GO/internal/nl/openai.go`、`CXX/docs/` | 建立完整 Go 侧地图并开始对接 C++ |

---

## 12. 你读完每一站以后，最好能自测这几个问题

### 读完入口层以后

- `run`、`serve`、`nl`、`shell` 四个子命令分别干什么？
- 哪个子命令不依赖模型？
- `buildService()` 为什么是入口里最值得盯的函数？

### 读完 service 层以后

- 写保护是在协议层做，还是 service 层也做？
- `TranslateNL()` 为什么要拿 catalog snapshot？
- 为什么模型输出还要再次校验？

### 读完 Action 层以后

- `RequestEnvelope` 和 `Request` 的区别是什么？
- `read_only` 为什么会在执行前就拦截写动作？
- `scan_primary_int_range` 为什么会要求索引？
- `insert_row` 的 JSON 值是在哪里变成 `haruhidb.Value` 的？

### 读完 cgo 层以后

- Go 里的 `Value` 怎样被传到 C API？
- 为什么这里要自己处理错误码和错误字符串？
- 为什么 `Scanner.Next()` 会返回 `io.EOF`？

如果这些问题都能独立回答，说明你对 Go 侧已经不只是“看过”，而是已经形成结构化理解了。

---

## 13. 推荐的实际阅读顺序清单

如果你希望按文件一条条过，下面这份可以直接照着走：

1. [`README.md`](README.md)
2. [`docs/go-usage-guide.md`](docs/go-usage-guide.md)
3. [`docs/action-protocol-v1.md`](docs/action-protocol-v1.md)
4. [`GO/cmd/haruhidb/main_test.go`](GO/cmd/haruhidb/main_test.go)
5. [`GO/haruhidb/haruhidb_test.go`](GO/haruhidb/haruhidb_test.go)
6. [`GO/cmd/haruhidb/main.go`](GO/cmd/haruhidb/main.go)
7. [`GO/cmd/haruhidb/config.go`](GO/cmd/haruhidb/config.go)
8. [`GO/internal/app/service.go`](GO/internal/app/service.go)
9. [`GO/internal/app/service_test.go`](GO/internal/app/service_test.go)
10. [`GO/internal/action/types.go`](GO/internal/action/types.go)
11. [`GO/internal/action/decode.go`](GO/internal/action/decode.go)
12. [`GO/internal/action/validator.go`](GO/internal/action/validator.go)
13. [`GO/internal/action/execute.go`](GO/internal/action/execute.go)
14. [`GO/internal/action/action_test.go`](GO/internal/action/action_test.go)
15. [`GO/internal/action/action_execute_test.go`](GO/internal/action/action_execute_test.go)
16. [`GO/internal/transport/http/server.go`](GO/internal/transport/http/server.go)
17. [`GO/internal/transport/http/server_test.go`](GO/internal/transport/http/server_test.go)
18. [`GO/internal/nl/types.go`](GO/internal/nl/types.go)
19. [`GO/internal/nl/openai.go`](GO/internal/nl/openai.go)
20. [`GO/haruhidb/doc.go`](GO/haruhidb/doc.go)
21. [`GO/haruhidb/haruhidb.go`](GO/haruhidb/haruhidb.go)
22. [`CXX/docs/README.md`](CXX/docs/README.md)
23. [`CXX/docs/architecture_overview.md`](CXX/docs/architecture_overview.md)
24. [`CXX/docs/execution_and_dataflow.md`](CXX/docs/execution_and_dataflow.md)

---

## 14. 最后一句建议

如果你真的想“从基层开始”，最靠谱的基层不是 cgo，也不是网络层，而是：

- `Action 请求如何被校验并执行`

因为这一层正好卡在：

- 上面能看到用户输入
- 下面能看到数据库 API

它是这个仓库 Go 侧最像“主梁”的地方。

所以最推荐的起点始终是：

- 文档建立地图
- 测试建立直觉
- `run -> service -> action -> db` 建立最小主线

只要这条线吃透，后面无论去看 HTTP、NL 还是 C++，都会简单很多。
