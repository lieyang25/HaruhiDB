# HaruhiDB 启动配置示例

这些示例用于 `--config` / `HARUHIDB_CONFIG` 启动方式。

- `serve-no-llm.json`：无模型模式（仅 action）
- `serve-openai.json`：OpenAI 网络模型模式
- `serve-ollama.json`：本地 Ollama 模式
- `nl-quicktest-openai.json`：模型直连快速测试（OpenAI，自动执行）
- `nl-quicktest-ollama.json`：模型直连快速测试（Ollama `qwen3:1.7b`，携带思考参数）
- `nl-quicktest-ollama-no-thinking.json`：模型直连快速测试（Ollama `qwen3:1.7b`，不携带思考参数）

其中 `serve-openai.json` 演示了：

- `llm.reasoning_effort`（`off/low/medium/high`）：切换思考强度
- `llm.examples_path`：让模型读取范例文档（提示词注入）

其中 `nl-quicktest-*.json` 演示了：

- `common.allow_write=true`：允许插入/删除这类写动作
- `nl.mode=read_write` + `nl.execute=true`：翻译后直接执行
- `nl.input`：自然语言输入也可预置在配置里（可不写 `--input`）
- `llm.examples_path`：先喂动作范例，再让模型输出
- `llm.reasoning_effort`：可测试“是否携带思考参数”
- `llm.stream=true`：对 `qwen3` 这类长思考模型更稳，避免非流式长时间不回包

示例命令：

```bash
cd /home/suzumiya/__code__/code/HaruhiDB/GO

go run ./cmd/haruhidb serve --config ../docs/configs/serve-no-llm.json
go run ./cmd/haruhidb serve --config ../docs/configs/serve-openai.json
go run ./cmd/haruhidb serve --config ../docs/configs/serve-ollama.json
```

模型直连快速测试（插入/查询/删除）：

```bash
cd /home/suzumiya/__code__/code/HaruhiDB
cp GO/haruhidb/test_output/quickstart_demo.db /tmp/haruhidb-nl-quicktest.db
cp GO/haruhidb/test_output/quickstart_demo.wal /tmp/haruhidb-nl-quicktest.wal || true

cd GO
# OpenAI:
export OPENAI_API_KEY=your_key
go run ./cmd/haruhidb nl --config ../docs/configs/nl-quicktest-openai.json

# Ollama:
# 先确认本地 Ollama 服务可用，且模型已拉取 qwen3:1.7b
ollama pull qwen3:1.7b

# 携带思考参数（reasoning_effort=medium）
go run ./cmd/haruhidb nl --config ../docs/configs/nl-quicktest-ollama.json

# 不携带思考参数（reasoning_effort=off）
go run ./cmd/haruhidb nl --config ../docs/configs/nl-quicktest-ollama-no-thinking.json

# 若要临时覆盖配置中的 nl.input，依然可加 --input：
go run ./cmd/haruhidb nl --config ../docs/configs/nl-quicktest-ollama.json --input "查询 student 表主键 id=1 的记录"
```

命令行参数会覆盖配置文件中的同名配置。

例如临时打开思考强度：

```bash
go run ./cmd/haruhidb serve \
  --config ../docs/configs/serve-openai.json \
  --reasoning-effort medium
```

完整参数示例（尽量配置化）见：

- [`config-full-example.json`](config-full-example.json)

也可以直接一键启动（自动检查 Ollama、拉模型并启动服务）：

```bash
cd /home/suzumiya/__code__/code/HaruhiDB
./scripts/serve_ollama_one_click.sh
```
