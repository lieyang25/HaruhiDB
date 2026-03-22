# HaruhiDB 启动配置示例

这些示例用于 `--config` / `HARUHIDB_CONFIG` 启动方式。

- `serve-no-llm.json`：无模型模式（仅 action）
- `serve-openai.json`：OpenAI 网络模型模式
- `serve-ollama.json`：本地 Ollama 模式

示例命令：

```bash
cd /home/suzumiya/__code__/code/HaruhiDB/GO

go run ./cmd/haruhidb serve --config ../docs/configs/serve-no-llm.json
go run ./cmd/haruhidb serve --config ../docs/configs/serve-openai.json
go run ./cmd/haruhidb serve --config ../docs/configs/serve-ollama.json
```

命令行参数会覆盖配置文件中的同名配置。

也可以直接一键启动（自动检查 Ollama、拉模型并启动服务）：

```bash
cd /home/suzumiya/__code__/code/HaruhiDB
./scripts/serve_ollama_one_click.sh
```
