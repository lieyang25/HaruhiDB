# HaruhiDB 项目架构与API文档(README版本)

补充阅读：

- [功能闭环审查与 Catalog 持久化补充说明](CXX/docs/functional_closure_review.md)
- [功能闭环审查更新（2026-03-18）](CXX/docs/functional_closure_review_update_20260318.md)

## 项目概述

HaruhiDB 是一个C++关系型数据库实现，采用模块化设计，分为7个主要层次：

```
┌─────────────────────────────────────────────────┐
│         Execution Layer (执行层)                 │
│  ├── SeqScanExecutor, FilterExecutor            │
│  ├── ProjectionExecutor, InsertExecutor         │
│  ├── DeleteExecutor, UpdateExecutor             │
│  ├── ValuesExecutor, IndexScanExecutor          │
│  └── AbstractExecutor, ExecutorContext          │
├─────────────────────────────────────────────────┤
│         Table Layer (表管理层)                   │
│  ├── TableHeap (表数据堆)                       │
│  └── TableIterator (表扫描迭代器)               │
├─────────────────────────────────────────────────┤
│         Catalog Layer (目录层)                   │
│  ├── Catalog (表目录)                           │
│  ├── TableInfo (表信息)                         │
│  ├── Schema (表结构)                            │
│  └── Column (列定义)                            │
├─────────────────────────────────────────────────┤
│         Buffer Layer (缓冲层)                    │
│  ├── BufferPoolManager (缓冲池管理)             │
│  └── LruKReplacer (页面替换)                    │
├─────────────────────────────────────────────────┤
│         Storage Layer (存储层)                   │
│  ├── Disk Management (磁盘管理)                 │
│  │   └── DiskManager                            │
│  ├── Page Management (页管理)                   │
│  │   ├── Page, BPlusTreePage                    │
│  │   ├── BPlusTreeLeafPage, BPlusTreeInternalPage
│  │   └── TablePage                              │
│  ├── Record Management (记录管理)               │
│  │   ├── RID, Tuple, TupleCodec                 │
│  ├── Index Management (索引管理)                │
│  │   ├── BPlusTree, IndexIterator               │
│  └── WAL (预写日志)                             │
│      └── WalManager                             │
├─────────────────────────────────────────────────┤
│         Type Layer (类型系统)                    │
│  ├── TypeId, TypeUtil                           │
│  └── Value (通用值表示)                         │
├─────────────────────────────────────────────────┤
│         Common Layer (公共配置)                  │
│  ├── Constants (PAGE_SIZE, HEADER_SIZE等)      │
│  └── ErrorCode (错误处理)                       │
└─────────────────────────────────────────────────┘
```

---

## 第一层: Type Module (类型系统)

**位置**: `CXX/src/include/type/`

### 1.1 TypeId 枚举
数据库支持的基本类型：BOOLEAN, TINYINT, SMALLINT, INTEGER, BIGINT, FLOAT, DOUBLE, DECIMAL, VARCHAR

### 1.2 TypeUtil (静态工具类)
```cpp
class TypeUtil {
  // 类型检验
  static constexpr bool IsValid(TypeId t);           // 类型有效性
  static constexpr bool IsVariableLength(TypeId t);  // 变长类型检查
  static constexpr bool IsIntegral(TypeId t);        // 整数检查
  static constexpr bool IsFloatingPoint(TypeId t);   // 浮点检查
  static constexpr bool IsNumeric(TypeId t);         // 数值检查
  
  // 类型信息
  static constexpr int FixedLengthSize(TypeId t);           // 固定长度大小
  static constexpr std::string_view TypeName(TypeId t);     // 类型名
  static inline std::optional<TypeId> ParseType(std::string_view name); // 字符串转类型
};
```

### 1.3 Value 类
通用值容器，使用std::variant存储不同类型数据。支持序列化/反序列化、类型转换、值比较。

**主要方法**:
- 构造和工厂方法: `Value()`, `Boolean()`, `Int32()`, `VarChar()`, `Null()`
- 类型检查: `IsNull()`, `Type()`, `TryAs<T>()`
- 值操作: `CanCastTo()`, `AsLongDouble()`, `Compare()`, `ToString()`
- 序列化: `Serialize()`, `TryDeserialize()`, `Deserialize()`

---

## 第二层: Buffer Layer (缓冲池管理)

**位置**: `CXX/src/include/buffer/`

缓冲池采用LRU-K替换算法，管理内存中的页面。

### 2.1 LruKReplacer 类
实现LRU-K页面替换策略。

**主要方法**:
- `explicit LruKReplacer(size_t pool_size, size_t k = 2)` - 构造
- `void RecordAccess(frame_id_t frame_id)` - 记录访问
- `void SetEvictable(frame_id_t frame_id, bool evictable)` - 标记可淘汰
- `bool Victim(frame_id_t& frame_id)` - 选择被淘汰的Frame
- `void Remove(frame_id_t frame_id)` - 移除Frame
- `size_t Size() const` - 获取可淘汰Frame数量

### 2.2 BufferPoolManager 类
缓冲池管理器，管理与磁盘的页面交互。

**主要方法**:
- `std::expected<storage::Page*,BufferPoolErr> FetchPage(page_id_t page_id)` - 获取页
- `std::expected<storage::Page*,BufferPoolErr> NewPage(page_id_t *page_id)` - 创建新页
- `bool UnpinPage(page_id_t page_id, bool is_dirty)` - Unpin页
- `bool DeletePage(page_id_t page_id)` - 删除页
- `std::expected<void,BufferPoolErr> FlushPage(page_id_t page_id)` - 刷新单页
- `std::expected<void,BufferPoolErr> FlushAllPages()` - 刷新所有页

---

## 第三层: Storage Layer (存储系统)

**位置**: `CXX/src/include/storage/`

### 3.1 Disk Manager (磁盘管理)

#### DiskManager 类
管理数据库文件的物理存储。

**主要方法**:
- `auto ReadPage(page_id_t page_id, page_data_t& data) -> std::expected<void,IOErr>` - 读页
- `auto WritePage(page_id_t page_id, const page_data_t& data) -> std::expected<void,IOErr>` - 写页
- `auto AllocatePage() -> std::expected<page_id_t,IOErr>` - 分配新页
- `auto DeallocatePage(page_id_t page_id) -> std::expected<void,IOErr>` - 释放页
- `auto Flush() -> std::expected<void,IOErr>` - 刷新

### 3.2 Page Management (页管理)

#### Page 类
内存中的页面表示。

**主要方法**:
- 初始化: `void InitBlank(page_id_t, PageType)`, `void ResetMetaData(page_id_t)`
- 访问元数据: `PersistentHeader* Header()`, `page_id_t PageId()`, `PageType Type()`
- Pin管理: `void Pin()`, `void UnPin()`, `int PinCount()`
- 脏标记: `void MarkDirty()`, `void ClearDirty()`, `bool IsDirty()`
- 并发访问: `void RLock()`, `void RUnLock()`, `void WLock()`, `void WUnLock()`
- 数据访问: `std::byte* RawData()`, `page_data_t& Data()`

#### BPlusTreePage 类 (B+树页基类)
**主要方法**:
- `bool InitForNewPage(page_id_t, PageType, uint16_t max_size, page_id_t parent = INVALID)`
- `page_id_t GetPageId()`, `PageType GetPageType()`
- `bool IsLeafPage()`, `bool IsInternalPage()`, `bool IsRootPage()`
- `page_id_t GetParentPageId()`, `void SetParentPageId(page_id_t)`
- `uint16_t GetSize()`, `uint16_t GetMaxSize()`, `uint16_t GetMinSize()`

#### BPlusTreeLeafPage 类
B+树叶子页，存储键值对(int32_t key -> RID value)。

**主要方法**:
- `bool InitForNewLeaf(uint16_t max_size, page_id_t parent = INVALID)`
- `page_id_t GetNextPageId()`, `void SetNextPageId(page_id_t)`
- `const MappingType& ItemAt(uint16_t index)`, `const KeyType& KeyAt(uint16_t index)`, `const ValueType& ValueAt(uint16_t index)`
- `uint16_t KeyIndex(const KeyType& key)`, `bool Lookup(const KeyType& key, ValueType* out_value)`
- `bool Insert(const KeyType&, const ValueType&)`, `bool Remove(const KeyType&)`
- `void MoveHalfTo()`, `void MoveAllTo()`, `bool MoveFirstToEndOf()`, `bool MoveLastToFrontOf()`

#### BPlusTreeInternalPage 类
B+树内部页，存储分支键和子页指针(int32_t key -> page_id_t child_page_id)。

**主要方法**:
- `bool InitForNewInternal(uint16_t max_size, page_id_t parent = INVALID)`
- `page_id_t GetLeftMostChild()`, `void SetLeftMostChild(page_id_t)`
- `const MappingType& ItemAt()`, `const KeyType& KeyAt()`, `bool SetKeyAt()`
- `page_id_t ChildAt()`, `bool FindChildIndex()`
- `page_id_t Lookup(const KeyType&)` - 根据键查找子页
- `bool PopulateNewRoot()`, `bool InsertAfter()`, `bool RemoveChildAt()`
- `void MoveHalfTo()`, `bool MoveFirstToEndOf()`, `bool MoveLastToFrontOf()`, `void MoveAllTo()`

#### TablePage 类
表数据页，管理行数据和Slot。

**主要方法**:
- `void InitForNewPage(page_id_t page_id)`
- `TablePageHeaderData* HeaderData()` - 获取页头数据
- `page_id_t NextPageId()`, `void SetNextPageId(page_id_t)`, `slot_id_t SlotCount()`
- `auto InsertTuple(const record::Tuple&) -> std::expected<slot_id_t, TablePageErr>` - 插入行
- `auto UpdateTuple(slot_id_t, const record::Tuple&) -> std::expected<void, TablePageErr>` - 更新行
- `auto MarkDelTuple(slot_id_t) -> std::expected<void, TablePageErr>` - 标记删除
- `auto GetTuple(slot_id_t, record::Tuple&) -> std::expected<void, TablePageErr>` - 获取行
- `bool TupleCountersConsistent()`, `void RepairTupleCounters()`
- `uint16_t AliveTupleCount()`, `uint16_t DeletedTupleCount()`
- `Slot* SlotArray()`, `Slot* GetSlot(slot_id_t)`, `uint16_t FreeSpace()`

### 3.3 Record Management (记录管理)

#### RID 类
Record Identifier，标识一条记录的位置(page_id + slot_id)。

**主要方法**:
- `RID(page_id_t page_id, slot_id_t slot_id)` - 构造
- `page_id_t GetPageId()`, `slot_id_t GetSlotId()`
- `void SetRID(page_id_t, slot_id_t)` - 设置RID
- `bool operator==(const RID&)` - 相等比较

#### Tuple 类
数据库元组，包含字节数据向量。

**主要方法**:
- `explicit Tuple(std::span<const std::byte> data)` - 从跨度构造
- `explicit Tuple(std::vector<std::byte> data)` - 从向量构造
- `uint16_t Size()` - 获取元组大小
- `const std::byte* Data()`, `std::byte* Data()` - 获取数据指针

#### TupleCodec 类
元组编解码器，在Value向量和Tuple二进制表示间转换。

**主要方法**:
- `static std::expected<Tuple, TupleCodecErr> Encode(const Schema&, std::span<const Value> values)` - 编码Value为Tuple
- `static std::expected<std::vector<Value>, TupleCodecErr> Decode(const Schema&, const Tuple&)` - 解码Tuple为Value
- `static std::expected<Value, TupleCodecErr> DecodeAt(const Schema&, const Tuple&, size_t column_index)` - 解码特定列

### 3.4 Index Management (索引管理)

#### IndexIterator 类
B+树索引迭代器。

**主要方法**:
- `IndexIterator()` - 默认构造(end迭代器)
- `IndexIterator(BufferPoolManager*, page_id_t leaf_page_id, uint16_t index)` - 构造
- `MappingType operator*()` - 解引用，返回(key, RID)对
- `IndexIterator& operator++()` - 前缀递增
- `bool operator==(const IndexIterator&)`, `bool operator!=(const IndexIterator&)`
- `bool IsEnd()` - 是否为结束迭代器

#### BPlusTree 类
B+树索引。

**主要方法**:
- `explicit BPlusTree(BufferPoolManager*)` - 构造(新树)
- `BPlusTree(BufferPoolManager*, page_id_t header_page_id)` - 构造(加载已有树)
- `bool IsEmpty()` - 是否为空
- `bool GetValue(int32_t key, RID* out_rid)` - 查询
- `bool Insert(int32_t key, const RID& rid)` - 插入
- `bool Remove(int32_t key)` - 删除
- `IndexIterator Begin()`, `IndexIterator Begin(int32_t key)`, `IndexIterator End()`
- `page_id_t RootPageId()`, `page_id_t HeaderPageId()`

### 3.5 WAL - Write-Ahead Logging (预写日志)

#### WalManager 类
管理数据库日志以支持恢复。

**主要方法**:
- `explicit WalManager(std::filesystem::path wal_path)` - 构造
- `bool AppendLog(const LogRecord&)` - 追加日志记录
- `bool FlushLog()` - 刷新日志
- `bool Recover(BufferPoolManager*)` - 恢复数据库
- `bool Redo(const LogRecord&, BufferPoolManager*)` - 重做日志

---

## 第四层: Catalog Layer (目录管理)

**位置**: `CXX/src/include/catalog/`

### 4.1 Column 类
表列的元数据描述。

**主要方法**:
- `Column(std::string name, TypeId type, bool nullable = true, std::optional<Value> = {})` - 固定长度列构造
- `Column(std::string name, TypeId type, uint32_t length, bool nullable, std::optional<Value> = {})` - 变长列构造
- `const std::string& Name()` - 列名
- `TypeId Type()` - 列类型
- `uint32_t Length()` - 列长度
- `bool Nullable()` - 是否允许NULL
- `bool IsInlined()`, `bool IsVarlen()` - 存储格式
- `uint32_t Offset()`, `void SetOffset(uint32_t)` - Tuple内Offset

### 4.2 Schema 类
表结构定义，包含多个Column，计算Tuple布局。

**主要方法**:
- `explicit Schema(std::vector<Column> columns)` - 构造
- `static std::expected<Schema, std::string> Create(std::vector<Column>)` - 安全创建
- `const std::vector<Column>& Columns()` - 获取所有列
- `size_t ColumnCount()` - 列数量
- `bool Empty()` - 是否为空
- `const Column& GetColumn(size_t index)`, `Column& GetColumn(size_t index)` - 按索引获取列
- `std::optional<size_t> TryGetColumnIndex(std::string_view name)` - 按名字查找列索引
- `size_t GetColumnIndex(std::string_view name)` - 按名字获取列索引(异常版本)
- `bool HasColumn(std::string_view name)` - 是否存在列
- `const Column* FindColumn(std::string_view name)`, `Column* FindColumn(std::string_view name)` - 按名字查找列
- `bool IsTupleInlined()` - Tuple是否完全内联(不含VARCHAR)

### 4.3 TableInfo 类
表的运行时信息(OID、名称、Schema、TableHeap)。

**主要方法**:
- `TableInfo(table_oid_t oid, std::string name, Schema schema, std::unique_ptr<TableHeap> table_heap)` - 构造
- `table_oid_t Oid()` - 表OID
- `const std::string& Name()` - 表名
- `const Schema& GetSchema()`, `Schema& GetSchema()` - 获取Schema
- `TableHeap* GetTableHeap()` - 获取TableHeap指针
- `bool HasTableHeap()` - 是否有有效TableHeap

### 4.4 Catalog 类
数据库表目录，管理所有表。

**主要方法**:
- `explicit Catalog(BufferPoolManager* bpm)` - 构造
- `std::expected<TableInfo*, std::string> CreateTable(std::string table_name, const Schema&)` - 创建表
- `TableInfo* GetTable(std::string_view table_name)` - 按名字查找表
- `TableInfo* GetTable(std::string_view table_name) const` - 按名字查找表(常数版本)

---

## 第五层: Table Layer (表管理)

**位置**: `CXX/src/include/table/`

### 5.1 TableIterator 类
在表中顺序扫描，跨页管理。

**主要方法**:
- `TableIterator()` - 默认构造(end迭代器)
- `TableIterator(TableHeap*, page_id_t start_page, slot_id_t start_slot)` - 构造
- `record::Tuple operator*()` - 解引用，返回当前行
- `record::RID GetRID()` - 获取当前行RID
- `TableIterator& operator++()` - 前缀递增
- `bool operator==(const TableIterator&)`, `bool operator!=(const TableIterator&)`
- `bool IsEnd()` - 是否为结束迭代器

### 5.2 TableHeap 类
表数据堆，管理表的页链和行操作。

**主要方法**:
- `static std::expected<std::unique_ptr<TableHeap>, std::string> Create(BufferPoolManager*)` - 创建表堆
- `explicit TableHeap(BufferPoolManager*, page_id_t first_page_id = INVALID_PAGE_ID)` - 构造
- `bool InsertTuple(const Tuple&, RID* out_rid = {})` - 插入行
- `bool GetTuple(const RID&, Tuple* out_tuple)` - 获取行
- `bool DeleteTuple(const RID&)` - 删除行
- `bool UpdateTuple(const RID&, const Tuple& new_tuple, RID* out_rid = {})` - 更新行
- `TableIterator Begin()`, `TableIterator End()` - 获取迭代器
- `void SetWalManager(WalManager*)`, `WalManager* GetWalManager()` - WAL管理
- `page_id_t FirstPageId()`, `void SetFirstPageId(page_id_t)` - 首页ID

---

## 第六层: Execution Layer (查询执行)

**位置**: `CXX/src/include/execution/`

### 6.1 ExecutorContext 类
执行器上下文，持有目录引用。

**主要方法**:
- `explicit ExecutorContext(Catalog*)` - 构造
- `Catalog* GetCatalog()` - 获取目录

### 6.2 AbstractExecutor 类 (执行器基类)
所有执行器的接口。

**主要方法**:
- `explicit AbstractExecutor(ExecutorContext*)` - 构造
- `virtual void Init() = 0` - 初始化
- `virtual bool Next(ExecutorRow* row) = 0` - 获取下一行

### 6.3 SeqScanExecutor 类
顺序扫描执行器。

**主要方法**:
- `SeqScanExecutor(ExecutorContext*, TableInfo*)` - 构造
- `void Init() override` - 初始化迭代器
- `bool Next(ExecutorRow* row) override` - 扫描下一行

### 6.4 FilterExecutor 类
过滤执行器(WHERE子句)。

**主要方法**:
- `FilterExecutor(ExecutorContext*, std::unique_ptr<AbstractExecutor> child, Predicate)` - 构造(child是数据源)
- `void Init() override` - 初始化
- `bool Next(ExecutorRow* row) override` - 返回满足谓词的行

### 6.5 ProjectionExecutor 类
投影执行器(SELECT子句)。

**主要方法**:
- `ProjectionExecutor(ExecutorContext*, std::unique_ptr<AbstractExecutor> child, std::vector<uint32_t> column_indices)` - 构造
- `void Init() override` - 初始化
- `bool Next(ExecutorRow* row) override` - 返回指定列的行

### 6.6 InsertExecutor 类
插入执行器。

**主要方法**:
- `InsertExecutor(ExecutorContext*, TableInfo*, std::unique_ptr<AbstractExecutor> child)` - 构造
- `void Init() override` - 初始化
- `bool Next(ExecutorRow* row) override` - 执行插入

### 6.7 DeleteExecutor 类
删除执行器。

**主要方法**:
- `DeleteExecutor(ExecutorContext*, TableInfo*, std::unique_ptr<AbstractExecutor> child)` - 构造
- `void Init() override` - 初始化
- `bool Next(ExecutorRow* row) override` - 执行删除

### 6.8 UpdateExecutor 类
更新执行器。

**主要方法**:
- `UpdateExecutor(ExecutorContext*, TableInfo*, std::unique_ptr<AbstractExecutor> child, Updater)` - 构造
- `void Init() override` - 初始化
- `bool Next(ExecutorRow* row) override` - 执行更新

### 6.9 ValuesExecutor 类
值执行器(VALUES子句)。

**主要方法**:
- `ValuesExecutor(ExecutorContext*, std::vector<std::vector<Value>> rows)` - 构造
- `void Init() override` - 初始化
- `bool Next(ExecutorRow* row) override` - 返回下一行

### 6.10 IndexScanExecutor 类
索引扫描执行器。

**主要方法**:
- `IndexScanExecutor(ExecutorContext*, TableInfo*, BPlusTree*, std::optional<int32_t> start_key = {})` - 构造
- `void Init() override` - 初始化迭代器
- `bool Next(ExecutorRow* row) override` - 扫描下一行

---

## 系统常量 (Common)

```cpp
constexpr size_t PAGE_SIZE = 4096;           // 页大小
constexpr size_t HEADER_SIZE = 32;           // 页头大小
constexpr uint32_t DB_MAGIC = 0x48415255;    // DB标识
constexpr uint32_t DB_VERSION = 1;           // DB版本

using page_id_t = uint32_t;                  // 页ID类型
using frame_id_t = size_t;                   // Frame ID类型
using table_oid_t = uint32_t;                // 表OID类型
using slot_id_t = uint16_t;                  // Slot ID类型
using lsn_t = uint64_t;                      // 日志序列号类型
```

---

## 数据流示例

### 插入数据流
```
Client Code
   ↓
Catalog::CreateTable() → TableInfo (Schema + TableHeap)
   ↓
TableHeap::InsertTuple(Tuple) 
   ↓ Tuple通过TupleCodec从Values编码
TablePage::InsertTuple()
   ↓ 在页中分配Slot
DiskManager::WritePage()  (如果页被Pin,最终通过BufferPoolManager持久化)
```

### 查询数据流
```
Client Code
   ↓
ExecutorContext(Catalog)
   ↓
SeqScanExecutor(TableInfo) → BufferPoolManager → TableHeap::TableIterator
   ↓ 遍历所有活跃行,返回ExecutorRow
FilterExecutor(child, predicate) → 过滤行
   ↓
ProjectionExecutor(child, columns) → 选择列
   ↓
ExecutorRow → 最终结果
```

### 索引查询流
```
IndexScanExecutor(BPlusTree, start_key)
   ↓
BPlusTree::Begin(key) → IndexIterator
   ↓ 查找键,得到RID
TableHeap::GetTuple(RID)
   ↓ 根据RID定位页和Slot
TablePage::GetTuple(slot_id)
   ↓
Tuple → TupleCodec::Decode() → Values
   ↓
ExecutorRow
```
