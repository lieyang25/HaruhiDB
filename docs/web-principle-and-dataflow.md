# Web 原理与数据流（Web-Only）

## 1. 架构

HaruhiDB Web 是“单进程内置前后端”：

- `GET /ui`：返回内置静态页面
- `GET /ui/*`：返回 `app.js/styles.css` 等资源
- `GET /v1/capabilities`：返回可执行动作与当前库信息
- `POST /v1/action`：执行动作

优点：

- 部署只需一个进程
- UI/API 同源，无额外 CORS
- 树莓派部署简单

## 2. 数据流

1. 页面初始化调用 `/v1/capabilities`
2. 页面渲染动作列表、mode 可用性、当前表信息
3. 用户在 JSON 控制台组装 envelope（`version=v3`）
4. 页面调用 `/v1/action`
5. 服务端校验并执行，返回统一响应结构：`ok/data/error/meta`

## 3. 关键约束

- 读请求使用 `mode=read_only`
- 写请求使用 `mode=read_write`
- 写入可被 `common.allow_write=false` 全局禁用
- `batch` 可封装多步骤操作
