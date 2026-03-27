# HaruhiDB 文档中心（Web-Only）

当前默认方案已经统一为：

- 树莓派运行 HaruhiDB Web 服务
- 本机浏览器访问 `http://<pi-ip>:8080/ui`
- 交互方式只保留 Action 协议（无模型、无 NL 翻译）

如果只看一篇，请先看：

- [Web 一页上手与动作速查](web-action-one-page-guide.md)

## 推荐阅读顺序

1. [Web 一页上手与动作速查](web-action-one-page-guide.md)
2. [配置参数总表](config-parameter-reference.md)
3. [配置文件说明](configs/README.md)
4. [Web 原理与数据流](web-principle-and-dataflow.md)

## 参考文档

- [Go 使用指南（serve/API）](go-usage-guide.md)
- [Python 客户端开发者文档](python-client-developer-guide.md)
- [Go API 与 Action 覆盖矩阵](go-api-action-capability-matrix.md)
- [常见错误排查](error-experience-playbook.md)
- [静态链接与远程运行说明](static-link-web-cli-and-remote-exec-guide.md)

## 历史协议资料

- `action-protocol-v1.md`
- `action-v1-model-spec.json`
- `action-v1-request-template.json`
- `action-v1-showcase-example.md`
- `action-v2-showcase-example.md`

说明：历史文件名保留为 `v1/v2`，但当前运行时统一按 `v3` Action 语义执行。
