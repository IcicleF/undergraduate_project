# Todo List

## Critical

* 读路径：读 k + Delta，取前 k 个解码
* Failure Detection
  * RDMA CM 可以检测 Disconnect 事件了，处理之

* ECAL 部分 block 写入
* ECAL 无复制写入（指针的 `Page`）

* 区分 send message 和 response message

Integration with LocoFS:
* cluster.conf 增加各节点类型，parseConfig 那里跟随修改

### 一些新想法

在 4kB block 被拆成若干份存储的前提下：

* RS(k+1, k) 在保证 block 原子写的情况下是无需一致性协议的
* RS(k, k) 无条件无需一致性协议（约定遵循 node id 小的节点）
* RS(k, p) (p < k - 1) 必须是分布式事务

在 4kB block 保持完整地塞进某个某个节点的情况下：

* 写 parity 必须是分布式事务（所以要打包写降低开销）
