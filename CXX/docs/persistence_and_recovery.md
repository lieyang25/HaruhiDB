# HaruhiDB 持久化与恢复

## 1. 持久化的基本视角

当前系统的持久化不是“对象序列化一切”，而是分成三类状态：

- 数据库文件头状态：由 `DiskManager` 保存在 Page 0 的 `DBHeader`
- 普通数据页 / 索引页状态：通过 `DiskManager` 管理的 `.db` 文件页保存
- 尚未安全落盘的页修改：通过 `WalManager` 保存在 `.wal` 文件

所以真正要问的不是“这个对象会不会持久化”，而是：

- 它有没有页级入口
- 它的入口页号是否可恢复
- 它的页内容是否已经刷入 `.db` 或可由 WAL 重放

## 2. 数据库文件布局

当前数据库文件最重要的布局是：

```text
Page 0   -> DBHeader
Page 1+  -> 普通数据页 / 索引页 / Catalog meta pages
```

`DBHeader` 当前保存：

- `magic_number`
- `version`
- `next_page_id`
- `free_list_head`
- `catalog_meta_page_id`

其中最关键的是 `catalog_meta_page_id`，因为它是整个对象层恢复的入口。

## 3. 各组件的持久化入口

### 3.1 Catalog

`Catalog` 的持久化入口不是某个进程内指针，而是：

- `DBHeader.catalog_meta_page_id`

Catalog 元数据当前持久化的核心内容包括：

- `table_oid`
- `table_name`
- `serialized_schema`
- `first_page_id`
- `index_oid`
- `index_name`
- `header_page_id`
- `next_table_oid`
- `next_index_oid`

因此，重启后 `Catalog` 可以根据元数据页链恢复：

- 表名与 OID
- schema
- `TableHeap` 首页
- 索引 header page

### 3.2 TableHeap

`TableHeap` 自身的核心持久化入口是：

- `first_page_id`

只要 `first_page_id` 还在，页链中的 `next_page_id` 还在，整张表的页链就可以被重新组织起来。

这也是为什么 `Catalog` 持久化 `first_page_id` 非常关键。

### 3.3 BPlusTree

`BPlusTree` 自身的核心持久化入口是：

- `header_page_id`

而 `header_page` 又会持久化：

- 当前 `root_page_id`

因此，恢复 B+Tree 的逻辑是两级入口：

```text
Catalog meta -> header_page_id -> root_page_id -> whole tree
```

### 3.4 普通页

无论是表页还是索引页，真正的长期状态都以页字节形式保存在 `.db` 文件里。

页是否真的进入 `.db`，依赖：

- 显式 `FlushPage()` / `FlushAllPages()`
- 被淘汰时刷盘
- 或者先写 WAL，随后在重启时执行 redo 恢复

## 4. BufferPoolManager 在持久化路径中的位置

`BufferPoolManager` 不负责“定义哪些对象应该持久化”，它负责：

- 把页取入内存
- 管理 pin/unpin
- 维护 dirty 状态
- 在刷盘或淘汰时把页交给 `DiskManager::WritePage()`

因此它是：

- 对象层的下层
- 持久化链路的中转层
- 恢复过程的目标页访问入口

## 5. WAL 的当前语义

### 5.1 当前记录形式

当前 WAL 采用整页 after-image 策略：

- 每条日志带一个 `DiskRecordHeader`
- 后面跟一个固定长度的整页 payload
- payload 当前固定为 `PAGE_SIZE`

这意味着它记录的不是“执行了什么逻辑操作”，而是“页面最终应该长什么样”。

### 5.2 当前写入触发点

当前页级写入链路里，`TableHeap` 是主要的 WAL 触发者。

典型路径是：

```text
TableHeap modifies page
  -> AppendPageAfterImageLogOrDie(page, type)
  -> WalManager::AppendLog(record)
  -> WalManager::FlushLog()
```

这保证了：

- 只要页修改已经发生，就会先把 after-image 刷进 WAL
- 即使 `.db` 还没来得及写回，也能在重启时 redo

### 5.3 当前恢复路径

恢复链路如下：

```text
WalManager::Recover(buffer_pool)
  -> 顺序扫描 WAL 文件
  -> 校验每条记录 header
  -> 把 after-image 覆盖回目标页
  -> BufferPoolManager::FlushAllPages()
  -> 截断 WAL 文件
```

恢复完成后：

- `.db` 文件获得恢复后的页状态
- `.wal` 被清空
- `next_lsn_` 重置

## 6. 系统级恢复链路

当前真正的系统恢复主线不是单独调用 `WalManager::Recover()`，而是放在启动入口里：

```text
DatabaseRuntime::Open()
  -> DiskManager
  -> BufferPoolManager
  -> WalManager::Recover()      [enable_wal]
  -> Catalog load
  -> rebuild TableInfo / TableHeap / BPlusTree
  -> bind WalManager
```

这条顺序决定了：

- 先恢复页，再恢复对象
- 避免 Catalog 看到尚未 redo 的旧页状态

## 7. 当前持久化/恢复保证矩阵

| 项目 | 当前状态 | 说明 |
| --- | --- | --- |
| DBHeader 持久化 | 已成立 | 由 `DiskManager` 维护 Page 0 |
| Catalog 元数据持久化 | 已成立 | 由 catalog meta page 链维护 |
| 表页持久化 | 已成立 | 依赖刷盘或 WAL redo |
| 索引页持久化 | 已成立 | 依赖刷盘或 WAL redo |
| 重启后自动发现表 | 已成立 | `Catalog` 可从 `catalog_meta_page_id` 自动恢复 |
| 重启后自动发现索引 | 已成立 | `Catalog` 可根据 `header_page_id` 加载索引 |
| 启动时统一恢复 | 已成立 | 由 `DatabaseRuntime::Open()` 组织 |
| redo 恢复 | 已成立 | `WalManager` 顺序重放 after-image |
| undo / rollback | 未支持 | 当前没有事务级回滚语义 |
| checkpoint | 未支持 | 当前恢复后直接截断 WAL |

## 8. 当前边界与注意事项

### 8.1 WAL 是 redo，不是事务系统

当前 WAL 能解决的是：

- 页修改已发生但 `.db` 尚未安全落盘时的崩溃恢复

它不能解决的是：

- 多语句事务回滚
- 崩溃时的逻辑级原子提交语义
- checkpoint 之前/之后的复杂恢复策略

### 8.2 索引一致性仍依赖标准写路径

虽然索引对象和表对象都能恢复，但“恢复后索引一定与表完全一致”仍依赖写入时是否经过规范路径。

当前最可靠的前提是：

- 表修改走执行层 DML
- 索引由执行层同步维护

如果调用方绕过执行层直接操作 `TableHeap`，恢复层不会替它修复索引语义。

### 8.3 刷盘语义仍应显式看待

当前代码中，“页被修改”不等于“页已经在 `.db` 里安全可见”。

要让状态稳定进入数据库文件，需要依赖：

- `FlushPage()` / `FlushAllPages()`
- 页淘汰写回
- 或 WAL 恢复后的统一刷盘

因此，在理解当前系统时，应把“内存页状态”和“数据库文件状态”明确区分开。
