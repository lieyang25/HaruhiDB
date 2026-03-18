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

## 构建

```bash
cmake -S CXX -B CXX/example/build -DTEST=OFF
cmake --build CXX/example/build --target haruhidb_quickstart
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
