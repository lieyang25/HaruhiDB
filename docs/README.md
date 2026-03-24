# HaruhiDB 文档中心（整合版）

如果你只看一篇，请先看：

- [一页上手与动作速查](web-action-one-page-guide.md)

上面这篇已经整合了启动流程、动作清单、动作示例、常见报错。

## 按场景选读

| 场景 | 先看哪篇 | 说明 |
| --- | --- | --- |
| SSH + 网页联调（最常见） | [一页上手与动作速查](web-action-one-page-guide.md) | 从配置到执行的最短路径 |
| 网页功能与两段式执行原理 | [Web 原理与信息流](web-principle-and-dataflow.md) | 为什么先翻译再执行 |
| 线上联调报错排查 | [错误经验汇总](error-experience-playbook.md) | 真实报错与修复 |
| 配置项查询 | [配置参数总表](config-parameter-reference.md) | 配置键与 CLI 参数映射 |

## 深入文档（需要时再看）

- 启动与 API 说明：`go-usage-guide.md`
- 远程联动与静态库：`static-link-web-cli-and-remote-exec-guide.md`
- 配置样例：`configs/README.md`
- 协议详规（历史命名保留）：`action-protocol-v1.md`
- 动作机器可读规范（历史命名保留）：`action-v1-model-spec.json`
- 旧版示例：`action-v1-showcase-example.md`、`action-v2-showcase-example.md`
- Go API 与动作覆盖矩阵：`go-api-action-capability-matrix.md`

## 当前建议

1. 新请求统一使用 `version="v3"`
2. 需要写入时使用 `mode="read_write"`
3. 先“仅翻译”，确认 `valid=true` 后再执行
