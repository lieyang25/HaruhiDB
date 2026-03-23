# HaruhiDB 静态库双选项 + Web/CLI 使用 + 远程联动指南

这份文档集中回答 3 个问题：

1. 内核能否做成 `Linux + Windows` 双平台静态库选项，并继续给 Go 调用
2. 网页脚本在哪里、怎么用；本地 CLI 和 Go `serve` 到底怎么运行
3. `PC + 虚拟机 + 树莓派` 是否能联动执行全流程，是否要改代码

---

## 1) 静态库双选项方案（Linux + Windows + 树莓派可落地）

结论：可以，而且建议做成“默认动态 + 可切静态”的双模式。

### 1.1 设计目标

- 默认行为不变：继续用当前动态库模式（兼容现有脚本和命令）
- 新增静态模式：`go` 通过 `-tags` 切换，避免运行时找 `.dll/.so`
- 覆盖平台：Windows、Linux（含树莓派 Linux）

### 1.2 CMake 侧方案

在 `CXX` 增加两个选项：

- `HARU_CAPI_BUILD_SHARED`（默认 `ON`）
- `HARU_CAPI_BUILD_STATIC`（默认 `OFF`，按需打开）

构建产物建议：

- 动态：`haruhidb_capi`（现有）
- 静态：`haruhidb_capi_static`（避免与 Windows import lib 名冲突）

建议在 `CXX/src/capi/CMakeLists.txt` 中同时声明 `SHARED` 和 `STATIC` 目标，二者都链接 `HaruhiDB`。

### 1.3 Go 侧方案

把当前 `#cgo` 声明拆成两份（`haruhidb` 包）：

- `cgo_shared.go`：默认编译（`!haru_static`）
- `cgo_static.go`：`//go:build haru_static`

使用方式：

- 动态模式：`go run ./cmd/haruhidb ...`
- 静态模式：`go run -tags haru_static ./cmd/haruhidb ...`

### 1.4 脚本层统一开关（Win/Linux）

建议给现有一键脚本加环境变量：

- `HARU_LINK_MODE=shared|static`

行为：

- `shared`：沿用当前逻辑（设置 `PATH` / `LD_LIBRARY_PATH`）
- `static`：构建静态库并用 `go run -tags haru_static ...`

### 1.5 对现有功能影响

- 默认保持 `shared`，所以对现有功能无破坏性影响
- 静态模式是新增能力，不会改变已有接口协议

---

## 2) 网页脚本、CLI、Go 服务怎么用

### 2.1 网页一键脚本在哪里

- Windows: `scripts/start_web_one_click.ps1`
- Linux: `scripts/start_web_one_click.sh`

### 2.2 网页启动命令

Windows（PowerShell）：

```powershell
cd E:\__code__\HaruhiDB
.\scripts\start_web_one_click.ps1
```

Linux（bash）：

```bash
cd /path/to/HaruhiDB
./scripts/start_web_one_click.sh
```

脚本会自动：检查依赖、拉模型、准备 CAPI 库、启动 Go `serve`、打开 `/ui`。

### 2.3 CLI 与 serve 的关系

- `run / nl / shell`：本地 CLI，单进程运行，不必须先起 `serve`
- `serve`：开启 HTTP 服务，供网页或远程机器调用

### 2.4 本地 CLI 最小命令

Action 直执行：

```powershell
cd E:\__code__\HaruhiDB\GO
go run ./cmd/haruhidb run --config ../docs/configs/serve-no-llm.json --json "{\"version\":\"v1\",\"request_id\":\"r1\",\"mode\":\"read_only\",\"action\":\"list_tables\",\"args\":{}}"
```

NL 翻译并执行（本地 Ollama）：

```powershell
cd E:\__code__\HaruhiDB\GO
go run ./cmd/haruhidb nl --config ../docs/configs/nl-quicktest-ollama-qwen2.5-coder-3b-strict.json --execute
```

### 2.5 手动启动 Go 服务（不走一键脚本）

```powershell
cd E:\__code__\HaruhiDB\GO
go run ./cmd/haruhidb serve --config ../docs/configs/serve-web-ollama-3b.json
```

启动后：

- Web: `http://127.0.0.1:8080/ui`
- 健康检查: `http://127.0.0.1:8080/healthz`

---

## 3) `AllocatePage: write new page failed` 为什么会发生

这个错误不等同于“必须管理员权限”。

更常见原因：

1. 目标数据库路径不可写（目录只读、路径异常、临时目录受限）
2. 文件被其他进程占用（同一个 db 文件被并发打开）
3. CAPI 运行库/工具链混用导致异常行为（Windows 下 MSVC/MinGW 混搭）
4. 沙箱/安全软件拦截写入（测试环境常见）

你当前现象更像是“环境写入问题”，不是 v2 协议逻辑本身错误。

建议排查顺序：

1. 用固定可写路径（不要依赖系统默认临时目录）
2. 确认没有第二个服务/进程占用同一 db
3. 用与你 Go cgo 一致的工具链产物（Windows 建议统一 MinGW 产物）
4. 先跑 `go run ... nl --execute` 小用例，再跑 `go test`

---

## 4) PC + 虚拟机 + 树莓派联动：要不要改代码

结论：基础联动不需要改代码，主要是网络与配置。

### 4.1 推荐拓扑

- 树莓派运行 HaruhiDB `serve`
- PC / 虚拟机通过 HTTP 调用树莓派

只要端口可达，网页、CLI、脚本都能远程调用。

### 4.2 树莓派启动建议

在树莓派上启动服务监听外网地址：

```bash
HARU_LISTEN=0.0.0.0:8080 ./scripts/start_web_one_click.sh
```

或：

```bash
cd /path/to/HaruhiDB/GO
go run ./cmd/haruhidb serve --config ../docs/configs/serve-web-ollama-3b.json --listen 0.0.0.0:8080
```

### 4.3 PC/虚拟机侧调用

只需把目标地址改为树莓派 IP：

- `http://<pi_ip>:8080/healthz`
- `http://<pi_ip>:8080/v1/nl/translate`
- `http://<pi_ip>:8080/v1/action`

### 4.4 端到端联动示例（Windows PowerShell）

```powershell
$Base = "http://192.168.1.50:8080"

# 1) 健康检查
Invoke-RestMethod -Method Get -Uri "$Base/healthz"

# 2) 先翻译
$translateReq = @{
  request_id = "pc-to-pi-001"
  input      = "严格只输出 4 步：insert/get/delete/get，表 student，key 303"
  mode       = "read_write"
} | ConvertTo-Json

$translateResp = Invoke-RestMethod -Method Post -Uri "$Base/v1/nl/translate" -ContentType "application/json" -Body $translateReq
$translateResp | ConvertTo-Json -Depth 30

# 3) valid=true 时再执行
if ($translateResp.valid -and $translateResp.candidate_envelope) {
  $actionReq = $translateResp.candidate_envelope | ConvertTo-Json -Depth 30
  $actionResp = Invoke-RestMethod -Method Post -Uri "$Base/v1/action" -ContentType "application/json" -Body $actionReq
  $actionResp | ConvertTo-Json -Depth 30
}
```

### 4.5 虚拟机网络注意点

- `Bridge` 模式：最直接，虚拟机与树莓派互通简单
- `NAT` 模式：需要额外端口转发

### 4.6 什么时候需要改代码

只有在你想要这些“增强能力”时才需要：

1. 任务队列/异步回调
2. 多节点调度（不是单机 HTTP 调用）
3. 更细粒度鉴权、审计链路

普通“PC 或 VM 发指令，树莓派执行并回 JSON”不需要改代码。

---

## 5) 当前 v2 状态（简版）

- 已实现：`create_table / drop_table / create_primary_int_index / drop_index` 与版本门控
- 已加：v2 归一化与关键测试
- 当前阻塞：你环境中的底层写页错误会影响“DDL 端到端稳定性验证”

建议先把“写页环境问题”定位稳定，再做 v2 最终验收。

