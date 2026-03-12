3.16:
1.现在发现的一个改进点在于page中，大量使用了std::memory_order_relaxed
虽然在buffer_pool_manager中的函数，都使用了大锁，这让不保证顺序的原子操作变得不那么危险
但如果改进性能，则要审查一下是否修改。
2.现在写下的std::expected存在部分没有完善，例如在buffer_pool_manager中的部分函数
备注：此优化完成

3.11:
1.我现在想到的一个改进点在tableheap，因为这里使用了单链表组织，或许可以改进性能
2.我想到了bufferpoolmanager中初始化页似乎没有接受“页类型”问题，我是直接初始化heap类型，是个问题
3.似乎pin页有多加的可能，或许需要回顾
4.未来对bufferpoolmanager还可以优化
5.在页头中加入alive_tuple_count，即记录当前有多少未删除的他tuple，可以优化tableiterator
可以优化ReclaimPageIfEmpty(),
加入deleted_tuple_count可以做页内合并优化
备注：此优化完成

3.12:
1.现在想到diskmanager中似乎存在，可能不太需要的刷盘操作，这个想法的由来是
有地方似乎只用写某些位置,不太确定，可能不是重要问题，或者想法出错。
2.建议给 Schema 加一个 ToString()