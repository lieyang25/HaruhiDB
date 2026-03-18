# HaruhiDB 数据库文件与 WAL 布局

这份文档专门回答一个偏“物理组织”的问题：

- 当前 `.db` 文件里到底放了什么
- 多张表和索引在同一个数据库文件里如何共存
- `.wal` 文件当前按什么格式组织
- 启动恢复时系统如何从这两个文件重新装配状态

它不是事务论文式说明，而是面向当前实现本身的布局说明。

## 1. 整体视角

当前实现里：

- 一个 `.db` 文件对应一个数据库实例
- 这个数据库实例里的所有表、索引、Catalog 元数据，都共享同一个全局页空间
- 一个 `.wal` 文件对应这个数据库实例的 redo 日志

所以当前不是：

- 每张表一个独立文件
- 每个索引一个独立文件

而是：

```text
database instance
├── xxx.db   # 数据库主文件，按页组织
└── xxx.wal  # redo 日志文件，顺序追加
```

## 2. `.db` 文件的基本布局

当前 `.db` 文件按固定大小页面组织：

- 每页大小固定为 `PAGE_SIZE = 4096`
- `page_id = 0` 的第一页固定是 `DBHeader`
- `page_id >= 1` 的页用于普通数据、索引和元数据

最简化的布局可以先理解成：

```text
.db file
├── Page 0   : DBHeader
└── Page 1+  : Catalog meta pages / HEAP pages / B+Tree pages / other pages
```

其中 `DBHeader` 当前保存：

- `magic_number`
- `version`
- `next_page_id`
- `free_list_head`
- `catalog_meta_page_id`

这几个字段里最重要的是：

- `next_page_id`：下一个可分配页号
- `free_list_head`：已回收可复用页链的入口
- `catalog_meta_page_id`：整个对象层恢复入口

## 3. 普通页如何区分类型

虽然 `.db` 文件里所有页都共享同一个全局页空间，但每一页自己的头部会记录“这是什么页”。

当前每个普通页开头都有一个 `PersistentHeader`，其中包含：

- `lsn`
- `page_id`
- `page_type`
- `opaque`

当前页类型主要包括：

- `HEAP`
- `INTERNAL`
- `LEAF`
- `HEADER`
- `FREELIST`

所以从物理文件角度看：

- 所有页都只是 4096 字节块
- 但页头里的 `page_type` 决定了后续该按 `TablePage` 还是 `B+TreePage` 去解释

## 4. 一张表在 `.db` 里如何组织

当前一张表不是一个文件，而是：

- `Catalog` 记录这张表的 `first_page_id`
- `TableHeap` 从这个首页开始，沿着 `next_page_id` 串起整张表的数据页链

逻辑上可以理解成：

```text
TableInfo(student)
  -> TableHeap(first_page_id = 2)

Page 2  (HEAP) -> Page 5 (HEAP) -> Page 9 (HEAP) -> INVALID
```

这里有两个关键点：

- 表的数据页不要求连续
- 表的页号可能和别的表、别的索引交错出现

因此，“一张表”在物理上并不是一段连续文件空间，而是“通过首页和页链组织起来的一组 HEAP 页”。

## 5. 多张表如何在同一个 `.db` 里共存

假设当前数据库里有两张表：

- `student`
- `course`

那么它们在同一个 `.db` 文件里，可能像这样分布：

```text
.db file
├── Page 0 : DBHeader
├── Page 1 : Catalog meta
├── Page 2 : student 的 HEAP 首页
├── Page 3 : 某个索引页
├── Page 4 : course 的 HEAP 首页
├── Page 5 : student 的第二个 HEAP 页
├── Page 6 : 其他页
├── Page 7 : course 的第二个 HEAP 页
└── ...
```

也就是说：

- `student` 通过 `first_page_id = 2` 找到自己的页链
- `course` 通过 `first_page_id = 4` 找到自己的页链

这两张表的页面完全可以交错分配，并不要求连续。

## 6. 索引在 `.db` 里如何组织

当前索引也是同一个 `.db` 文件里的一组页，而不是独立文件。

索引的物理入口是：

- `header_page_id`

而索引真正的树结构入口是：

- `header_page` 里保存的 `root_page_id`

逻辑上可以理解成：

```text
Catalog meta
  -> index header_page_id = 3

Page 3  (HEADER)
  -> root_page_id = 8

Page 8  (LEAF or INTERNAL)
  -> ... whole B+Tree
```

因此索引的恢复链路是：

```text
Catalog meta -> header_page_id -> root_page_id -> whole B+Tree
```

## 7. 一个“两个表 + 一个索引”的完整例子

如果数据库里有：

- 表 `student`
- 表 `course`
- 索引 `idx_student_id`

那么当前 `.db` 文件的逻辑布局可以想象成：

```text
.db file
├── Page 0   : DBHeader
│             - next_page_id = 10
│             - free_list_head = INVALID
│             - catalog_meta_page_id = 1
│
├── Page 1   : Catalog meta page
│             - table: student
│               oid = 0
│               first_page_id = 2
│
│             - table: course
│               oid = 1
│               first_page_id = 6
│
│             - index: idx_student_id
│               index_oid = 0
│               table_oid = 0
│               header_page_id = 3
│
├── Page 2   : student 的 HEAP 首页
├── Page 3   : idx_student_id 的 B+Tree header page
├── Page 4   : idx_student_id 的根页/叶子页
├── Page 5   : student 的第二个 HEAP 页
├── Page 6   : course 的 HEAP 首页
├── Page 7   : 其他页
├── Page 8   : course 的第二个 HEAP 页
└── Page 9   : 后续新页
```

这个例子最想说明的是：

- `.db` 是全局页空间
- 表和索引共享这个空间
- 真正区分“谁属于谁”的不是页号连续性，而是入口页和页内结构

## 8. `.wal` 文件的基本布局

当前 `.wal` 文件不是页空间结构，而是顺序追加的日志流。

当前组织形式是：

```text
.wal file
├── DiskRecordHeader
├── after-image page payload
├── DiskRecordHeader
├── after-image page payload
├── DiskRecordHeader
├── after-image page payload
└── ...
```

其中每条日志固定由两部分组成：

### 8.1 日志头 `DiskRecordHeader`

当前字段包括：

- `magic`
- `version`
- `type`
- `lsn`
- `page_id`
- `payload_len`

其大小当前固定为 32 字节。

### 8.2 日志 payload

当前 payload 固定是一整页 after-image：

- 大小固定为 `PAGE_SIZE`
- 内容是修改后页面的完整镜像

因此当前 WAL 记录的不是“插入了哪一行”，而是：

- “page_id = X 的页面修改后应该长成什么样”

## 9. 当前 `.wal` 记录什么，不记录什么

当前 WAL 是最小 redo WAL，特点是：

- 记录整页 after-image
- 只做 redo，不做 undo
- 不做 checkpoint
- 恢复完成后截断整个 WAL 文件

所以它能保证的是：

- 页面修改已经发生，但 `.db` 尚未安全落盘时，重启后还能把页面恢复回来

它不能保证的是：

- 事务级回滚
- 多语句原子提交
- 复杂 checkpoint 恢复

## 10. `.db` 和 `.wal` 在写入路径中的关系

当前写入路径可以简化理解成：

```text
TableHeap modifies page
  -> append full-page after-image to WAL
  -> flush WAL
  -> page may be flushed to .db later
```

也就是说：

- `.wal` 先承担“崩溃前保险”
- `.db` 再承担“长期稳定状态”

所以“数据已经写入数据库”在当前实现里至少有两种状态：

- 已写入 WAL，但还未刷入 `.db`
- 已经刷入 `.db`

这两种状态不能混为一谈。

## 11. 启动恢复时，`.db` 和 `.wal` 如何一起工作

当前启动恢复链路是：

```text
DatabaseRuntime::Open()
  -> DiskManager
  -> BufferPoolManager
  -> WalManager::Recover()
  -> Catalog load
  -> rebuild TableInfo / TableHeap / BPlusTree
```

这里顺序非常重要：

1. 先打开 `.db` 文件，恢复头页状态
2. 再扫描 `.wal`，把 after-image 重放回目标页
3. 把恢复后的脏页统一刷回 `.db`
4. 截断 `.wal`
5. 最后再通过 `catalog_meta_page_id` 恢复对象层

换句话说，恢复时是：

- 先恢复页状态
- 再恢复对象关系

这样 `Catalog` 才不会看到尚未 redo 的旧页面内容。

## 12. 当前布局理解里最容易混淆的点

### 12.1 `.db` 不是单表文件

它是整个数据库文件，内部混放：

- Catalog 元数据页
- 表页
- 索引页

### 12.2 表不是“连续文件片段”

表当前是：

- `first_page_id` 作为入口
- 多个 `HEAP` 页通过页链组织

### 12.3 索引不是“跟着表放在一起”

索引和表页共享同一个全局页空间，但它自己的入口是：

- `header_page_id`

因此物理上可能与表页交错分布。

### 12.4 WAL 不是数据库副本

`.wal` 只是尚未安全落盘修改的 redo 材料，不是完整数据库镜像。

## 13. 一句话总结

当前布局可以压缩成一句话：

**`.db` 是一个全局页空间容器，`Catalog` 提供表和索引的入口页；`.wal` 是顺序追加的整页 after-image redo 日志，用于在 `.db` 未及时落盘时恢复页面状态。**
