# HaruhiDB 文档中心（Web + Action）

当前默认方案：

- 树莓派或本机运行 HaruhiDB `serve`
- 浏览器访问 `http://<host>:8080/ui`
- API 统一通过 Action 协议（`POST /v1/action`）

如果只看一篇，请先看：

- [Web 一页上手与动作速查](web-action-one-page-guide.md)

## 1. 快速开始

- [Web 快速上手](web-quickstart.md)
- [Web 一页上手与动作速查](web-action-one-page-guide.md)

## 2. 运行与配置

- [Go 使用指南（serve/API）](go-usage-guide.md)
- [配置参数总表](config-parameter-reference.md)
- [配置文件说明](configs/README.md)
- [Web 原理与数据流](web-principle-and-dataflow.md)

## 3. 协议与接口

- [Python 客户端开发者文档](python-client-developer-guide.md)
- [Go API 与 Action 覆盖矩阵](go-api-action-capability-matrix.md)
- [Action 协议说明（历史文件名 v1）](action-protocol-v1.md)

## 4. 排障与部署

- [常见错误排查](error-experience-playbook.md)
- [静态链接与远程运行说明](static-link-web-cli-and-remote-exec-guide.md)

## 5. 历史协议资料

- `action-v1-model-spec.json`
- `action-v1-request-template.json`
- `action-v1-showcase-example.md`
- `action-v2-showcase-example.md`

说明：历史文件名保留为 `v1/v2`，但当前运行时统一按 `v3` Action 语义执行。
