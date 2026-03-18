# HaruhiDB 文档索引

`CXX/docs/` 现在不再保存偏阶段性的开发审查记录，而是专门描述当前实现本身。

这套文档的目标是：

- 说明当前数据库内核已经实现了什么
- 说明各层职责边界、依赖关系和主要对象
- 说明运行入口、执行路径、持久化与恢复链路
- 说明当前实现成立的前提、约束和边界

## 推荐阅读顺序

1. [系统架构总览](architecture_overview.md)
2. [运行与数据链路](execution_and_dataflow.md)
3. [持久化与恢复](persistence_and_recovery.md)
4. [当前实现使用方法](integration_guide.md)

如果只是想快速恢复全局理解，先读第 1 篇；如果要继续改代码，建议按上面的顺序读完。

实际可运行的示例代码见 [../example/README.md](../example/README.md)。

## 文档分工

### [architecture_overview.md](architecture_overview.md)

回答下面这些问题：

- 当前系统整体分几层
- 每一层负责什么、不负责什么
- 核心对象之间如何组织
- 为什么 `DatabaseRuntime / Catalog / TableHeap / BPlusTree / BufferPoolManager` 是主干对象

### [execution_and_dataflow.md](execution_and_dataflow.md)

回答下面这些问题：

- 数据库如何启动
- 执行器树如何组织
- 一次顺序扫描、索引扫描、插入、更新、删除如何穿过系统
- 执行层和存储层在什么位置衔接

### [persistence_and_recovery.md](persistence_and_recovery.md)

回答下面这些问题：

- 什么状态会持久化到 `.db` 或 `.wal`
- `Catalog`、`TableHeap`、`BPlusTree` 的恢复入口分别是什么
- `BufferPoolManager`、`DiskManager`、`WalManager` 如何配合
- 当前恢复语义有哪些边界

### [integration_guide.md](integration_guide.md)

回答下面这些问题：

- 当前内核最推荐怎样被外层调用
- 建表、建索引、插入、扫描、更新、删除分别怎么驱动
- 外层应该依赖哪些对象，避免直接踩到哪些底层层次
- 下一步如果要做数据库门面层，最适合从哪里收口

## 使用原则

后续更新文档时，建议始终遵循 3 条原则：

1. 只描述当前代码已经成立的实现，不写“计划中设计”冒充现状。
2. 优先写职责边界和依赖方向，不把文档退化成 API 罗列。
3. 任何结论都尽量能在 `CXX/src/` 或 `CXX/test/` 中找到对应实现或测试依据。
