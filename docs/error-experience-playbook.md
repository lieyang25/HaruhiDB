# HaruhiDB 错误经验汇总（Web + Ollama）

这份文档记录网页联调时最常见的问题，按“现象 -> 原因 -> 修复 -> 验证”给出。

## 1) 模型输出一堆无关动作

现象：

- 模型返回了 `list_tables / describe_table / scan_all / update_by_primary_int` 等与目标无关动作。
- JSON 可能合法，但执行计划偏离任务。

原因：

- 小模型在动作规划时更容易发散。
- 输入约束不够硬，模型会先做无关“环境检查”。

修复：

- 优先使用 `qwen2.5-coder:3b`。
- 使用“严格四步”提示词，显式禁止无关动作。
- 先点“仅翻译”，确认 `candidate_envelope` 再执行。

验证：

- `candidate_envelope.args.requests` 只包含预期步骤。

## 2) UI 翻译报错：`table "student" not found`

现象：

- 返回 `valid=false`，错误信息类似：
  `TRANSLATION_ERROR: NOT_FOUND: requests[0]: table "student" not found`

原因：

- 当前 `HARU_DB_PATH` 指向空库或不包含目标表。

修复（Linux/macOS 示例）：

```bash
export HARU_DB_PATH="$PWD/haruhidb-web.db"
cp -f ./GO/haruhidb/test_output/quickstart_demo.db  "${HARU_DB_PATH}"
cp -f ./GO/haruhidb/test_output/quickstart_demo.wal "${HARU_DB_PATH%.db}.wal"
```

验证：

- 再次点击“仅翻译”应返回 `valid=true`。

## 3) Shell 提示脚本不存在

现象：

- `./scripts/start_web_one_click.sh: No such file or directory`

原因：

- 当前目录不在仓库根目录。

修复：

```bash
cd /path/to/HaruhiDB
./scripts/start_web_one_click.sh
```

验证：

- 能看到服务启动日志，并打开 `http://127.0.0.1:8080/ui`（若未关闭自动打开）。

## 4) Linux/macOS 下 C API 动态库路径问题

现象：

- 启动时报找不到 CAPI 相关动态库（`libharuhidb_capi.so`/`libharuhidb_capi.dylib`）。

原因：

- `CXX/build/src/capi` 未加入运行时库搜索路径，或库尚未构建。

修复：

- 优先使用一键脚本：`./scripts/start_web_one_click.sh`（会自动检查与构建）。
- 手动方式可设置：
  `export LD_LIBRARY_PATH=/path/to/HaruhiDB/CXX/build/src/capi:${LD_LIBRARY_PATH:-}`

验证：

- `go run ./cmd/haruhidb serve ...` 能正常启动，无库加载错误。

## 5) 最稳的验收用例（建议固定）

推荐固定为“严格四步闭环”：

1. `insert_row(id=303,name='instruct_test')`
2. `get_by_primary_int(key=303)` -> `found=true`
3. `delete_by_primary_int(key=303)`
4. `get_by_primary_int(key=303)` -> `found=false`

只要这个用例稳定通过，当前“模型约束 + 网页闭环 + 执行链路”就基本可用。
