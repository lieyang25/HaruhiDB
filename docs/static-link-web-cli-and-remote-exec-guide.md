# 静态链接与远程运行说明（Web-Only）

## 当前形态

- CLI 仅保留 `serve`
- 远程访问统一通过 Web/API

## 动态链接（默认）

```bash
cd /home/suzumiya/__code__/code/HaruhiDB
./scripts/start_web_one_click.sh
```

脚本会：

1. 准备 C API 动态库（缺失时自动构建）
2. 启动 Go `serve`
3. 暴露 UI + API

## 静态链接（可选）

如果你已有静态构建流程，可继续沿用；对接口行为没有差异。

## 远程联调最小检查

```bash
curl -s http://<pi-ip>:8080/healthz
curl -s "http://<pi-ip>:8080/v1/capabilities"
```

接口主线：`/ui`、`/v1/action`、`/v1/capabilities`、`/healthz`。
