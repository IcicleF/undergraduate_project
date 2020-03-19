# Todo List

## Critical

* 读路径：读 k + Delta，取前 k 个解码
* 写路径：写到固定位置即可（一个虚拟地址对应一个物理位置）。如果有 failed node 那么进行降级写，node 恢复后读出期间哪些块被写了，逐块解码恢复。
  * 暂定 2PC，**需要创新**。
* Failure Detection

### 一些新想法

在 4kB block 被拆成若干份存储的前提下：

* RS(k+1, k) 在保证 block 原子写的情况下是无需一致性协议的
* RS(k, k) 无条件无需一致性协议（约定遵循 node id 小的节点）
* RS(k, p) (p < k - 1) 必须是分布式事务

在 4kB block 保持完整地塞进某个某个节点的情况下：

* 写 parity 必须是分布式事务（所以要打包写）

## Normal

* 不同的节点类型
* `MemoryConfig` 单独放在一个文件里
