# HaruhiDB Examples

当前目录用于放置“可直接运行的使用示例”，重点不是测试内部细节，而是展示当前内核应该怎样被调用。

## quickstart

文件：`quickstart.cxx`

这个示例会完整演示当前推荐使用路径：

- 打开 `DatabaseRuntime`
- 创建表和索引
- 插入数据
- 顺序扫描
- 过滤与投影
- 索引扫描
- 更新与删除
- 刷盘
- 重新打开数据库并验证恢复结果

## haruhidb_inspect

文件：`haruhidb_inspect.cxx`

这个工具会把当前实现生成的 `.db` / `.wal` 文件做最小可用的结构化检查，重点是“看懂当前落盘内容”，而不是导出 JSON 或做一致性检查。

V1 当前支持两个子命令：

- `db`
- `wal`

默认行为：

- `haruhidb_inspect db <file>` 等价于 `--summary`
- `haruhidb_inspect wal <file>` 等价于 `--summary`

其中 `db` 适合直接观察：

- 当前数据库文件一共有多少页
- `DBHeader` / free list / catalog meta 链
- 每张表和每个索引的 catalog 摘要
- 指定 heap page 里的 slot / tuple 值
- 指定索引 header/root 页的结构化信息

`wal` 直接暴露当前真实物理日志格式：

- entry 数量
- `put / delete` 数量统计
- 每条 entry 的 `type / lsn / page_id / payload_len`
- after-image 页头摘要和 payload preview

V1 暂不支持：

- `--json`
- `check`
- `--tables` / `--indexes`
- `--page-range`
- `--tree`
- `--type`

## 构建

```bash
cmake -S CXX -B CXX/example/build -DTEST=OFF
cmake --build CXX/example/build --target haruhidb_quickstart
cmake --build CXX/example/build --target haruhidb_inspect
```

## 运行

默认会把构建产物放到 `CXX/example/build/`，并把示例运行生成的
`quickstart_demo.db` / `quickstart_demo.wal` 放到可执行文件所在目录下，
也就是 `CXX/example/build/example/`：

```bash
./CXX/example/build/example/haruhidb_quickstart
```

也可以自己指定数据库文件路径：

```bash
./CXX/example/build/example/haruhidb_quickstart /tmp/haruhi_demo.db
```

## 使用 haruhidb_inspect

先准备一个 `.db` 文件，然后运行：

```bash
./CXX/example/build/example/haruhidb_inspect db \
  ./CXX/example/build/example/quickstart_demo.db --summary
```

查看 WAL：

```bash
./CXX/example/build/example/haruhidb_inspect wal \
  ./CXX/example/build/example/quickstart_demo.wal --summary
```
