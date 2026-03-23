# HaruhiDB 错误经验汇总（实战版）

这份文档专门记录你这次联调中出现过的真实问题，按“现象 -> 原因 -> 修复 -> 验证”整理，后续可直接对照处理。

## 1) 模型输出一堆无关动作

现象：

- 模型返回了 `list_tables / describe_table / scan_all / update_by_primary_int` 等与目标无关动作。
- JSON 可能是合法的，但执行计划偏离任务。

原因：

- 小模型（如 `qwen2.5-coder:0.5b-instruct`）在“规划动作”时更容易发散。
- 提示词约束不够硬，模型会先“自查环境”。

修复：

- 优先使用 `qwen2.5-coder:3b`。
- 使用“严格四步”提示词，显式禁止无关动作。
- 保留 `examples_path=../docs/action-v1-model-spec.json` 作为动作边界。

验证：

- 在 UI 或 CLI 中先跑“仅翻译”。
- 确认 `candidate_envelope.args.requests` 只有预期 4 步动作。

## 2) UI 翻译报错：`table "student" not found`

现象：

- 返回 `valid=false`，错误信息类似：
  `TRANSLATION_ERROR: NOT_FOUND: requests[0]: table "student" not found`

原因：

- 当前 `HARU_DB_PATH` 指向了空库或不包含 `student` 表的库。

修复（Windows 示例）：

```powershell
$env:HARU_DB_PATH = "$env:TEMP\haruhidb-web.db"
Copy-Item -Force .\GO\haruhidb\test_output\quickstart_demo.db  $env:HARU_DB_PATH
Copy-Item -Force .\GO\haruhidb\test_output\quickstart_demo.wal ($env:HARU_DB_PATH -replace '\.db$','.wal')
```

验证：

- 再次点“仅翻译”应返回 `valid=true`。
- 点“翻译并执行”后，4 步动作应全部 `ok=true`。

## 3) PowerShell 提示脚本不存在

现象：

- `.\scripts\start_web_one_click.ps1 : ... CommandNotFoundException`

原因：

- 当前目录不在仓库根目录（`E:\__code__\HaruhiDB`）。

修复：

```powershell
cd E:\__code__\HaruhiDB
.\scripts\start_web_one_click.ps1
```

验证：

- 能看到服务启动日志，并自动打开 `http://127.0.0.1:8080/ui`（若未关闭自动打开）。

## 4) 需要查看模型原始输出（不先看 JSON 校验）

现象：

- 你怀疑是模型本身输出有问题，想先看原文。

修复：

```powershell
$env:HARUHIDB_NL_DEBUG_RAW='1'
go run ./cmd/haruhidb nl --config ../docs/configs/nl-quicktest-ollama-qwen2.5-coder-3b-strict.json
```

验证：

- 响应 `meta.raw_output` 中可直接看到模型原始文本，用于判断“模型问题”还是“解析/校验问题”。

## 5) Windows 下 C API 动态库路径问题

现象：

- 启动时报找不到 CAPI 相关动态库或导入库。

原因：

- `CXX/build/src/capi` 未加入运行时 `PATH`，或库还没构建完成。

修复：

- 优先使用一键脚本：`.\scripts\start_web_one_click.ps1`（会自动检查和构建）。
- 手动方式需把 `E:\__code__\HaruhiDB\CXX\build\src\capi` 加入 `PATH`。

验证：

- `go run ./cmd/haruhidb serve ...` 能正常启动，无库加载错误。

## 6) 最稳的验收用例（建议固定）

推荐固定为“严格四步闭环”：

1. `insert_row(id=303,name='instruct_test')`
2. `get_by_primary_int(key=303)` -> `found=true`
3. `delete_by_primary_int(key=303)`
4. `get_by_primary_int(key=303)` -> `found=false`

只要这个用例在你的环境长期稳定通过，当前“模型约束 + 网页闭环 + 执行链路”就算完成度很高。