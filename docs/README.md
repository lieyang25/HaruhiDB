# HaruhiDB 文档总入口

这份索引用于解决“文档很多，不知道先看哪篇”的问题。

## 按目标查文档

| 你要做什么 | 先看哪里 | 说明 |
| --- | --- | --- |
| 最快跑起来（Web/服务/Ollama） | [Go 使用指南](go-usage-guide.md) | 单入口 serve、网页/API 示例、常见报错 |
| 使用网页 UI | [网页快速上手](web-quickstart.md) | 一键脚本启动、UI 访问、常见问题 |
| 理解网页原理与信息流 | [Web 原理与信息流](web-principle-and-dataflow.md) | 页面架构、信息流、输出结构、可改造点 |
| 做静态库与远程联动排障 | [静态库与远程联动指南](static-link-web-cli-and-remote-exec-guide.md) | Linux/macOS 静态库方案、Web/serve 用法、远程联动 |
| 排查联调报错 | [错误经验汇总](error-experience-playbook.md) | 真实踩坑与修复命令（模型/UI/启动） |
| 用配置文件启动 | [配置示例](configs/README.md) | `--config`、`HARUHIDB_CONFIG`、现成 JSON 配置 |
| 查所有支持参数 | [配置参数总表](config-parameter-reference.md) | 配置键、类型、CLI 参数一一对应 |
| 看 Action 协议字段和约束 | [Action 协议 v1](action-protocol-v1.md) | 请求/响应格式、字段约束、错误码 |
| 看 Go 完整 API 与动作覆盖 | [Go API 与动作覆盖矩阵](go-api-action-capability-matrix.md) | Go 完整能力、v1 缺口、v2 兼容策略 |
| 给模型喂“动作集合” | [动作机器可读规范](action-v1-model-spec.json) | 动作、参数、限制的机器可读清单 |
| 快速拿一个请求模板 | [请求模板](action-v1-request-template.json) | 最小可用信封模板 |
| 看协议实例（成功/失败） | [功能演示](action-v1-showcase-example.md) | 一组可直接参考的示例 |
| 看 v2 动作集示意 | [Action v2 功能演示](action-v2-showcase-example.md) | 包含 create/drop table/index 的 batch 示例 |
| 理解 Go 层代码结构 | [Go 阅读指南](../GO_READING_GUIDE.md) | 从入口到执行链路的阅读顺序 |
| 理解 C++ 内核架构 | [CXX 文档索引](../CXX/docs/README.md) | 架构、执行链路、WAL、文件布局 |

## 推荐阅读路径

### 路线 A：使用者（先跑通）

1. [网页快速上手](web-quickstart.md)
2. [Web 原理与信息流](web-principle-and-dataflow.md)
3. [静态库与远程联动指南](static-link-web-cli-and-remote-exec-guide.md)
4. [错误经验汇总](error-experience-playbook.md)
5. [Go 使用指南](go-usage-guide.md)
6. [配置示例](configs/README.md)
7. [配置参数总表](config-parameter-reference.md)
8. [Action 协议 v1](action-protocol-v1.md)

### 路线 B：提示词/Agent 集成者

1. [Action 协议 v1](action-protocol-v1.md)
2. [Go API 与动作覆盖矩阵](go-api-action-capability-matrix.md)
3. [动作机器可读规范](action-v1-model-spec.json)
4. [请求模板](action-v1-request-template.json)
5. [功能演示](action-v1-showcase-example.md)
6. [Action v2 功能演示](action-v2-showcase-example.md)

### 路线 C：代码开发者

1. [Go 阅读指南](../GO_READING_GUIDE.md)
2. [Go 使用指南](go-usage-guide.md)
3. [Go API 与动作覆盖矩阵](go-api-action-capability-matrix.md)
4. [CXX 文档索引](../CXX/docs/README.md)

